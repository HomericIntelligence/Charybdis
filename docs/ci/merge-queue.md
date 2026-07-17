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
- `.github/workflows/container.yml`
- `.github/workflows/integration-tests.yml`
- `.github/workflows/static-analysis.yml`

The required `build`, `unit-tests`, `test`, `lint`, and `package` contexts are
result fan-ins in the workflows that perform the underlying work. Each waits
for its `needs` jobs with `always()` and fails from the upstream result, so a
merge-group check cannot pass before the real build, test, lint, or package job
has completed. The single authoritative `test` context is emitted by the Build
and Test workflow for `pull_request`, `push`, and `merge_group`, and its
`needs: [build-test]` fan-in checks the real matrix result. The separate
`Integration Tests` workflow emits only the required `integration-tests`
context from its real suite; it does not emit another same-named `test` job.
This preserves both prior coverage paths without allowing a skipped job or an
early pass from one workflow to satisfy the other's required context.
`_required.yml` contains independent checks only; it does not create
merge-group skip/pass proxies for these contexts. Container builds have
read-only package permissions; only the trusted `push` to `main` publish job
receives `packages: write`. The release publisher remains tag/manual-only and
must not run for merge groups.

Run the executable policy regression test with:

```bash
./scripts/test-merge-queue-policy.py
```

The same test is required by the `merge-queue-policy` just recipe, `just ci`,
the pre-commit hook, and the required CI workflow's schema-validation job.

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
