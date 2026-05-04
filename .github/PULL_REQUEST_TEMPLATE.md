## Summary

<!-- What does this PR do? Link to the issue it closes. -->

Closes #

## Changes

- 

## Test Plan

- [ ] `just build` passes
- [ ] `just test` passes (all unit tests green)
- [ ] `just lint` passes (no clang-tidy errors)
- [ ] `just format-check` passes
- [ ] Integration tests (if applicable): ran against local Agamemnon+NATS
- [ ] Sanitizer run: `cmake --preset debug && cmake --build --preset debug && ctest --preset debug`

## Checklist

- [ ] Fault injection tests clean up after themselves (`DELETE /v1/chaos/*`)
- [ ] New tests use `hi.test.*` subjects, not production subjects
- [ ] No new magic numbers — constants are named
- [ ] No `using namespace` in new test files
