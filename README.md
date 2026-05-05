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

## Building

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

## License

MIT
