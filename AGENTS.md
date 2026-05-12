# AGENTS.md — Multi-Agent Coordination for ProjectCharybdis

This document specifies how agents in the HomericIntelligence mesh interact with
ProjectCharybdis, which NATS subjects are used for task routing and metrics, and the
handoff protocol for fault injection and recovery validation.

## Agent Roles in the Mesh

| Agent | Role |
|-------|------|
| **Agamemnon** | Orchestrator. Exposes the chaos REST API (`/v1/chaos/*`) and manages JetStream streams. |
| **Nestor** | Coordination agent. Schedules and monitors tasks across the mesh. |
| **Myrmidon workers** | Pull consumers. Execute tasks routed via `hi.myrmidon.hello.*`. |
| **Argus** | Metrics consumer. Ingests resilience events from `hi.logs.>` into Prometheus. |
| **Charybdis** | External test harness — not an agent. Invokes Agamemnon's chaos API and asserts recovery. |

Charybdis is not a participant in the agent mesh. It exercises the mesh from the outside
by injecting faults and observing whether the system returns to a healthy state.

## How Charybdis Invokes Chaos

All fault injection goes through Agamemnon's REST API. Charybdis does not modify NATS
directly.

### Endpoints

| Method | Path | Effect |
|--------|------|--------|
| `POST` | `/v1/chaos/network-partition` | Split Tailscale nodes; test NATS JetStream reconnect |
| `POST` | `/v1/chaos/latency` | Inject latency on a node; test backpressure and rate limiting |
| `POST` | `/v1/chaos/kill` | Kill a named service; test restart and recovery |
| `POST` | `/v1/chaos/queue-starve` | Stall all pull consumers on a subject; test no message loss |
| `GET` | `/v1/chaos` | List active faults (returns `{"faults": [...]}`) |
| `DELETE` | `/v1/chaos/{id}` | Remove an active fault by ID |

### Fault Response Shape

Every successful `POST /v1/chaos/<type>` returns a JSON object:

```json
{
  "id": "<fault-id>",
  "type": "<fault-type>",
  "active": true
}
```

The `id` field is required for the cleanup `DELETE` call.

### `queue-starve` Targeting

`POST /v1/chaos/queue-starve` is currently invoked by Charybdis tests with **no JSON
body** (see `test/src/test_chaos_api.cpp` and `test_chaos_resilience.cpp`). With an
empty body, Agamemnon stalls all pull consumers it knows about — this is the default
behaviour exercised by the resilience suite.

To target a specific NATS subject (rather than every subscriber), POST a JSON body of
the form:

```json
{ "subject": "hi.test.<scenario>.<random-suffix>" }
```

Production subjects (`hi.myrmidon.hello.*`, `hi.logs.>`) must never be passed as the
`subject` field. Tests that need to exercise these patterns must mint an isolated
subject via `random_suffix()` per the **Isolation invariant** below.

### Example Lifecycle

```bash
# 1. Inject a network partition
curl -s -X POST http://localhost:8080/v1/chaos/network-partition

# 2. Verify the fault is active
curl -s http://localhost:8080/v1/chaos

# 3. Assert recovery (poll until healthy or timeout)
# (done in test code via wait_until())

# 4. Remove the fault
curl -s -X DELETE http://localhost:8080/v1/chaos/<fault-id>
```

## NATS Subject Namespaces

| Subject | Purpose |
|---------|---------|
| `hi.myrmidon.hello.*` | Task routing to Myrmidon pull consumers |
| `hi.logs.>` | Resilience metrics and events → Argus/Prometheus |

**Isolation invariant:** Charybdis targets only test-namespaced subjects during fault
injection. Production subjects (`hi.myrmidon.hello.*`, `hi.logs.>`) must never be
directly targeted by fault injection. Tests that exercise these subjects create isolated
instances with a unique suffix (via `random_suffix()`) to avoid cross-test interference.

## Handoff Protocol

The standard chaos scenario follows this sequence:

1. **Inject** — Charybdis `POST`s a fault to `/v1/chaos/<type>`. Agamemnon applies the
   fault to the mesh node or service and returns a fault `id`.

2. **Observe** — Charybdis records the pre-fault state (e.g., task count, stream lag,
   health check result) as a baseline for comparison.

3. **Assert recovery** — Charybdis polls the system using `wait_until()` with a 30-second
   default timeout. The predicate checks that the mesh has returned to a healthy state
   (e.g., Agamemnon `/health` responds, tasks reach `completed` status). The timeout is
   per-call: pass a `std::chrono::seconds` value as the second argument to override the
   default for slow fault types (e.g. network-partition recovery often needs 60–120 s):

   ```cpp
   // Default 30s
   const bool ok = wait_until([&]() { return client_->is_healthy(); });

   // Custom timeout for a slow-recovery scenario
   const bool recovered = wait_until(
       [&]() { return client_->is_healthy(); },
       std::chrono::seconds{120});
   ```

   See `include/projectcharybdis/test_helpers.hpp` for the full signature.

4. **Clean up** — Regardless of assertion outcome, Charybdis `DELETE`s the fault via
   `/v1/chaos/<fault-id>`. Cleanup must happen even on test failure to avoid leaving
   faults active between test runs.

5. **Validate baseline** — Charybdis performs a final assertion that the system state
   matches the pre-fault baseline (e.g., no messages were lost, stream offsets are
   consistent).

Tests skip automatically (via `GTEST_SKIP()`) when Agamemnon is unreachable, so CI
environments without a live mesh do not fail the build.

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `AGAMEMNON_URL` | `http://localhost:8080` | Base URL for the Agamemnon REST API |
| `NATS_URL` | `nats://localhost:4222` | NATS server URL for direct subject access |

These are read at test startup via `agamemnon_url()` and `nats_url()` from
`include/projectcharybdis/test_helpers.hpp`.

## Security Boundary

Charybdis is authorized to inject faults only within the bounds of a chaos testing
engagement. The following constraints are enforced:

- Fault injection targets only test-namespaced NATS subjects. No production subject
  must be directly disrupted outside of an intentional resilience test.
- The `DELETE /v1/chaos/{id}` cleanup step is mandatory. Tests that fail to clean up
  faults are considered broken regardless of assertion outcome.
- CI integration tests skip when Agamemnon is unreachable rather than failing; this
  prevents flaky failures from blocking unrelated work.

For responsible disclosure of security issues, see [SECURITY.md](SECURITY.md).
