# Merge queue readiness

Charybdis is prepared for a staged GitHub merge-queue rollout on `main`.
This repository change adds merge-group event support only; it does not
activate the queue.

## Policy contract

[`configs/github/merge-queue-policy.json`](../../configs/github/merge-queue-policy.json)
is the machine-readable source of truth for the exact required contexts and
approved queue rule. Inspect it with:

```bash
jq -r '.required_contexts[]' configs/github/merge-queue-policy.json
jq '.merge_queue_rule' configs/github/merge-queue-policy.json
```

The required-context carriers support `push` and `pull_request` on `main` plus
`merge_group` with the `checks_requested` action:

- `.github/workflows/_required.yml`
- `.github/workflows/build-test.yml`
- `.github/workflows/code-coverage.yml`
- `.github/workflows/static-analysis.yml`

`.github/workflows/integration-tests.yml` also handles `merge_group` so the
integration suite executes in the queue, but its `integration-tests-run` job
is not a required-context carrier.

The required `integration-tests` context is emitted exactly once by the
`integration-tests` fan-in job in `.github/workflows/_required.yml`. The
dedicated integration workflow keeps its execution result separate as
`integration-tests-run`; it is diagnostic and does not replace the live
required context. The release publisher remains tag/manual-only and must not
run for merge groups.

Run the executable policy regression test with:

```bash
./scripts/test-merge-queue-policy.py
```

## Activation boundary

Odysseus issue #386 is the sole activation authority. Charybdis automation and
contributors must not mutate live rulesets or classic branch protection as part
of this readiness change. An independent human must review the workflow changes
before merge.

After this pull request merges, the Odysseus operator must snapshot the complete
live ruleset, verify its required contexts against the policy artifact, append
only the artifact's `merge_queue_rule`, and retain the snapshot for rollback.
The operator must then queue a representative pull request and record the live
ruleset response, the correlated `merge_group` check run, and the queued squash
merge on Charybdis issue #280.

Issue #280 remains open until activation and the queued smoke are complete.
