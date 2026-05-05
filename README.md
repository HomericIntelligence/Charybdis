# ProjectCharybdis

Chaos and resilience testing for the HomericIntelligence distributed agent mesh.

Part of [Odysseus](https://github.com/HomericIntelligence/Odysseus) — the HomericIntelligence meta-repo.

## Role

Charybdis injects faults via Agamemnon's `/v1/chaos/*` REST API and validates that the
HomericIntelligence mesh survives failures gracefully: network partitions, latency, service
kills, and queue starvation.

## Data Handling

Charybdis processes only chaos fault parameters, NATS subject identifiers, and ephemeral
test fixture metadata. It never collects agent task payload content, user PII, or production
message bodies. See the [Data Handling Policy](PRIVACY.md) for full details before running
Charybdis against a mesh that carries sensitive workloads.

## Prerequisites

- [Pixi](https://pixi.sh/) ≥ 0.24 — manages all other dependencies
- CMake ≥ 3.20 (installed by Pixi)
- Ninja ≥ 1.11 (installed by Pixi)
- GCC ≥ 12 or Clang ≥ 15 (installed by Pixi via `cxx-compiler ≥ 1.7`)
- Conan ≥ 2.0 (installed by Pixi)
- clang-tools ≥ 17 (installed by Pixi — provides clang-format and clang-tidy)
- [Just](https://just.systems/) — command runner

## Quick Start

```bash
# Clone and enter the repo
git clone https://github.com/HomericIntelligence/ProjectCharybdis.git
cd ProjectCharybdis

# Activate the Pixi environment
pixi shell

# Install Conan deps, configure, and build
just build

# Run all tests
just test
```

Alternatively, using raw CMake after `conan install`:

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

## Environment Variables

These variables configure which services Charybdis connects to during testing.
Both are optional; defaults point to local development instances.

| Variable        | Required | Default                    | Description                          |
|-----------------|----------|----------------------------|--------------------------------------|
| `AGAMEMNON_URL` | No       | `http://localhost:8080`    | Base URL of the Agamemnon chaos API  |
| `NATS_URL`      | No       | `nats://localhost:4222`    | NATS server used for JetStream tests |

Example:

```bash
export AGAMEMNON_URL=http://agamemnon.internal:8080
export NATS_URL=nats://nats.internal:4222
just test
```

## Architecture

```text
ProjectCharybdis/
├── src/
│   ├── main.cpp                  # Entry point
│   └── http_test_client.cpp      # HTTP client for the chaos API
├── include/projectcharybdis/
│   ├── http_test_client.hpp      # HTTP client interface
│   ├── test_helpers.hpp          # agamemnon_url(), nats_url(), wait_until()
│   └── version.hpp               # Version constants
├── test/src/
│   ├── test_chaos_api.cpp        # Integration tests against the chaos API
│   ├── test_helpers_unit.cpp     # Unit tests for test_helpers.hpp
│   ├── test_http_client_unit.cpp # Unit tests for the HTTP client
│   ├── test_malformed_messages.cpp
│   ├── test_payload_fuzzing.cpp
│   ├── test_protocol_correctness.cpp
│   └── test_main.cpp             # GoogleTest main
└── scripts/
    ├── lint.sh                   # clang-tidy runner
    ├── format.sh                 # clang-format runner
    └── coverage.sh               # gcovr report generator
```

Charybdis communicates exclusively with Agamemnon's chaos API:

| Endpoint                           | Effect                                              |
|------------------------------------|-----------------------------------------------------|
| `POST /v1/chaos/network-partition` | Partition Tailscale nodes; verify NATS reconnects   |
| `POST /v1/chaos/latency`           | Inject latency on a node; verify backpressure       |
| `POST /v1/chaos/kill`              | Kill a named service; verify restart/recovery       |
| `POST /v1/chaos/queue-starve`      | Stall pull consumers on a subject; verify no loss   |
| `DELETE /v1/chaos/*`               | Remove any injected fault                           |

Resilience metrics are reported to `hi.logs.>` for ingestion by Argus/Prometheus.

## Test Scenarios

| Scenario              | What Is Injected                          | What Is Validated                                   |
|-----------------------|-------------------------------------------|-----------------------------------------------------|
| Network partitions    | Split Tailscale nodes                     | NATS JetStream reconnects; no message loss          |
| Latency injection     | High-latency links on target node         | Backpressure and rate limiting hold                 |
| Service kills         | Kill Agamemnon / Nestor / Telemachy       | Process restart and state recovery                  |
| Queue starvation      | Exhaust myrmidon pull consumers           | No message loss; consumers resume cleanly           |
| Cascade failures      | Trigger failure in one component          | Blast radius is contained; other components healthy |

## Development

All recipes are run inside `pixi shell` or prefixed with `pixi run`:

```text
just build          # Install Conan deps, configure, and build (debug)
just test           # Run all tests via CTest
just lint           # Run clang-tidy on all sources
just format         # Auto-format all source files (clang-format v17)
just format-check   # Check formatting without modifying files
just coverage       # Build with coverage and generate gcovr report
just ci             # Full CI pipeline: build + test (CI preset)
just clean          # Remove build/ and install/
```

Install pre-commit hooks to enforce formatting and commit message conventions:

```bash
pre-commit install
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for branch naming, commit message format, pull
request requirements, and code review expectations.

## Documentation

- [AGENTS.md](AGENTS.md) — Multi-agent coordination: how agents invoke Charybdis, NATS subjects, and handoff protocols
- [CONTRIBUTING.md](CONTRIBUTING.md) — Development setup, workflow, and code standards
- [SECURITY.md](SECURITY.md) — Responsible disclosure process

## Troubleshooting

**Tests fail with connection refused**

`AGAMEMNON_URL` or `NATS_URL` is not set and no local service is running on the default
ports. Either start the services or point the variables at a running instance:

```bash
export AGAMEMNON_URL=http://your-agamemnon-host:8080
export NATS_URL=nats://your-nats-host:4222
```

**Build fails with "conan: command not found"**

You are not inside the Pixi environment. Run `pixi shell` first, or prefix commands with
`pixi run`:

```bash
pixi run just build
```

## License

MIT
