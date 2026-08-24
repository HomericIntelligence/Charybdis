# ThreadSanitizer Suppression Triage

This runbook defines the standing procedure for adding entries to `tsan.supp`.
Suppressions are added **only** in response to an observed ThreadSanitizer
failure — never speculatively.

## When This Applies

Only after a TSan CI run on `main` or a pull request fails with a race report.
If no CI run has produced a `WARNING: ThreadSanitizer:` block, this document
does not apply and `tsan.supp` must stay empty.

## Locate the Failure

1. Open the [Sanitizers workflow](.github/workflows/sanitizers.yml) run in the
   GitHub Actions tab (or via `gh run list --workflow sanitizers.yml`).
2. Open the failed `tsan` job.
3. Expand the failing `Test` step.
4. Find the `WARNING: ThreadSanitizer: data race` block. Record:
   - the race type (`data race`, `heap-use-after-free`, `destroy of a locked mutex`, …)
   - the top stack frames (mangled or demangled symbol names)
   - which test binary and test name were running.

## Decide: Suppress or Fix

Work down the decision tree in order:

1. **Is the race in our code?** → Fix it; do not suppress. A race in
   Charybdis sources (`src/`, `include/`) is always a real bug under TSan.
2. **Is the race inside a known lock-free library** (relaxed atomics, hazard
   pointers, RCU)? → Candidate for a `race:` suppression with the fully
   qualified symbol pattern.
3. **Is the race inside a transitively-linked system library we cannot modify**
   (e.g. glibc internals)? → Candidate for a `called_from_lib:` suppression.

Anything that does not fit one of these buckets must be fixed, not suppressed.

## Add the Entry

Append to `tsan.supp`. Each entry **must** be preceded by a comment recording:

- failing test name
- CI run URL
- date in ISO-8601 format
- one-line rationale for why the race is benign
- link to upstream issue, if any

Example shape (do not add this entry; it is illustrative only):

```text
# Test: test_http_client_retry_unit.RetryBackoffUnderLoad
# CI run URL: https://github.com/HomericIntelligence/Charybdis/actions/runs/<id>
# Date: 2026-08-23
# Rationale: <library> uses relaxed atomics internally; safe per upstream docs.
# Upstream: <link>
race:<fully::qualified::symbol::pattern>
```

Prefer namespace-qualified patterns (`race:ns::Class::method`) over bare symbol
names (`race:method`). Bare patterns silently suppress unrelated callers that
merely share a function name.

## Verify Locally

Confirm the previously-failing test now passes under TSan:

```bash
cmake --preset tsan
cmake --build --preset tsan
ctest --preset tsan
```

## PR Checklist

- Link the failing CI run in the PR body.
- Reference the failing test name in both the PR body and the `tsan.supp` comment.
- Never combine a suppression entry with unrelated code changes.
- The `Sanitizers` workflow must be green on the PR before merge.

## Forbidden

Do not add speculative entries — i.e. entries authored without an observed
failure. An empty suppressions file is the correct state until the TSan job
surfaces a real false positive.
