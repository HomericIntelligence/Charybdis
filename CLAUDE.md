# CLAUDE.md — Charybdis

## Project Overview

Charybdis is the chaos and resilience testing service for the HomericIntelligence
distributed agent mesh.

**Role:** Injects faults into the live system via Agamemnon's chaos API, then validates
that the system recovers gracefully. Proves the HomericIntelligence mesh is resilient.

## What Charybdis Tests

- **Network partitions** — split Tailscale nodes, verify NATS JetStream handles reconnects
- **Latency injection** — high-latency links, verify backpressure and rate limiting hold
- **Service kills** — kill Agamemnon/Nestor/Telemachy processes, verify restart/recovery
- **Queue starvation** — exhaust myrmidon pull consumers, verify no message loss
- **Cascade failures** — trigger failure in one component, verify blast radius is contained

## Architecture

Charybdis talks to Agamemnon's chaos API:

- `POST /v1/chaos/network-partition` — partition nodes in the Tailscale mesh
- `POST /v1/chaos/latency` — inject latency on a node
- `POST /v1/chaos/kill` — kill a named service
- `POST /v1/chaos/queue-starve` — stall all pull consumers on a subject
- `DELETE /v1/chaos/*` — remove injected faults

Resilience metrics are reported via `hi.logs.>` → Argus/Prometheus.

## Development Guidelines

- Language: C++20 exclusively
- Build: `cmake --preset debug` / `cmake --build --preset debug`
- Test: `ctest --preset debug`
- All tool invocations via `scripts/` wrappers
- Never `--no-verify`. Never merge with red CI.

## Sanitizers

Two CMake presets are available for runtime error detection:

| Preset | Sanitizers enabled | Detects |
|--------|--------------------|---------|
| `debug` | AddressSanitizer + UBSan | Heap/stack overflows, use-after-free, undefined behaviour |
| `tsan` | ThreadSanitizer | Data races between threads |

**Mutual-exclusivity constraint:** ASan and TSan instrument memory at the same level and
cannot be combined in a single build. Always use separate build directories.

```bash
# ASan + UBSan (default debug preset)
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

# TSan — separate build, separate ctest invocation
cmake --preset tsan
cmake --build --preset tsan
ctest --preset tsan
```

The `SANITIZER` CMake cache variable controls which sanitizer is injected; the presets set
it automatically. Do not pass `-DSANITIZER=` manually unless you know what you are doing.
