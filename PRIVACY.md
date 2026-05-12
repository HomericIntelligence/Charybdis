# Data Handling Policy — ProjectCharybdis

Charybdis is a chaos and resilience testing tool for the HomericIntelligence distributed
agent mesh. It interacts with Agamemnon's chaos REST API and NATS JetStream subjects. This
document describes what data Charybdis processes, where it flows, and how it is handled.

## What Data Charybdis Processes

Charybdis processes only the metadata necessary to inject and validate chaos faults:

- **Chaos API parameters** — fault type (network-partition, latency, kill, queue-starve),
  target node/service names, fault duration, and the fault IDs returned by Agamemnon.
- **NATS subject identifiers** — subject names targeted by queue-starvation faults and task
  routing validation (e.g. `hi.myrmidon.hello.*`). Subject names are structural identifiers;
  Charybdis does not inspect or log message bodies flowing through those subjects.
- **Test fixture metadata** — synthetic team names, agent names, labels, and task descriptions
  created during protocol-correctness tests. These are ephemeral and scoped to the test run.
- **Resilience metrics** — fault injection events and recovery observations reported to
  `hi.logs.>` for ingestion by Argus/Prometheus. Payloads contain fault IDs, timestamps, and
  pass/fail outcomes — no agent task payload content.

## Where Data Flows

| Data surface | Destination | Notes |
| --- | --- | --- |
| Chaos API requests | Agamemnon REST API (`POST /v1/chaos/*`) | Loopback or internal Tailscale mesh only; never routed to the public internet |
| Fault metadata responses | In-process test assertions | Not persisted beyond the test run |
| Task routing validation | NATS JetStream (`hi.myrmidon.hello.*`) | Subject names only; message bodies are synthetic fixtures |
| Resilience metrics | NATS JetStream (`hi.logs.>`) → Argus/Prometheus | Retention governed by the Argus configuration in ProjectArgus |
| Test output (stdout/stderr) | Local ctest output / CI logs | Ephemeral per test run; CI logs are subject to the GitHub Actions log retention policy |

## What Is Never Collected

Charybdis explicitly does not collect, log, or transmit:

- Agent task payload content (instructions, results, or any user-supplied task data)
- User personally identifiable information (PII)
- Secrets, tokens, or credentials flowing through the mesh
- Production NATS message bodies
- Any data outside the `hi.test.*` namespace when running against a mesh that carries
  sensitive workloads (see operator guidance below)

## Log Retention

- **Test output** — ephemeral; discarded when the ctest process exits.
- **CI logs** — retained according to the GitHub Actions log retention policy for the
  HomericIntelligence organisation (default: 90 days). Logs contain fault parameters and
  pass/fail outcomes, not payload content.
- **Resilience metrics (`hi.logs.>`)** — retention is governed by the Argus stream
  configuration in ProjectArgus. Charybdis has no control over downstream retention.
  The authoritative configuration lives in
  [`HomericIntelligence/ProjectArgus`](https://github.com/HomericIntelligence/ProjectArgus)
  under `configs/jetstream/` (stream name: `HI_LOGS`, subject filter: `hi.logs.>`).
  Operators verifying compliance should consult that repo for the current
  `MaxAge` / `MaxBytes` / `Discard` policy and the stream's owner team. If the
  stream is renamed or its retention policy changes, this section must be updated
  in the same PR.

## Sensitive Environment Guidance

When running Charybdis against a mesh that carries sensitive agent workloads:

1. **Namespace chaos targets** — restrict fault injection to nodes/services operating
   exclusively on `hi.test.*` subjects so that production subjects are never affected.
2. **Review subject filters** — confirm that queue-starvation tests target only test-scoped
   consumers before execution.
3. **Isolate CI secrets** — Agamemnon connection strings and NATS credentials used by
   Charybdis should be scoped to a test environment and rotated independently of production
   credentials.
4. **Audit CI log access** — ensure that CI log visibility is restricted to authorised
   personnel if test runs are executed against a staging mesh that mirrors production data.

See the [Security Policy](SECURITY.md) for the full set of isolation requirements and the
responsible disclosure process for security findings.

## Contact

- **Security vulnerabilities** — follow the process in [SECURITY.md](SECURITY.md).
- **Data governance questions** — open a GitHub Discussion in this repository.
