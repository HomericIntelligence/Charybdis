# Static Analysis in ProjectCharybdis

## Tools Configured

`cmake/StaticAnalyzers.cmake` enables two static analysis tools simultaneously:

- **clang-tidy** — via `CMAKE_CXX_CLANG_TIDY`
- **cppcheck** — via `CMAKE_CXX_CPPCHECK`

Both run on every translation unit during a normal `cmake --build` invocation.

## clang-tidy

clang-tidy findings are treated as **build errors**. The `.clang-tidy` config enables
`WarningsAsErrors`, so any clang-tidy diagnostic fails the build immediately.

## cppcheck

cppcheck runs with `--enable=all --inconclusive --inline-suppr` but its output goes to
`stderr` interleaved with other build output. Key differences from clang-tidy:

- cppcheck findings do **not** fail the build. CMake's `CMAKE_CXX_CPPCHECK` integration
  does not propagate cppcheck's exit code as a build error.
- cppcheck is therefore **advisory-only by design**: it surfaces additional diagnostics
  (e.g. unused variables, missing `override`) without blocking CI on inconclusive results.
- Developers should review cppcheck output in their local builds and address genuine issues,
  but CI will not go red from a cppcheck finding alone.

## Rationale for Keeping cppcheck Advisory

cppcheck's `--inconclusive` mode produces a higher false-positive rate than clang-tidy.
Promoting cppcheck to a hard error would require a curated suppression list and regular
maintenance. Until that investment is made, advisory mode is the appropriate posture.

## Opting Out Per Build

Both tools can be disabled individually without touching the source:

```bash
cmake --preset debug \
  -DProjectCharybdis_ENABLE_CPPCHECK=OFF   # disable cppcheck only
  -DProjectCharybdis_ENABLE_CLANG_TIDY=OFF # disable clang-tidy only
```

The Docker release build disables both (`-DENABLE_CLANG_TIDY=OFF -DENABLE_CPPCHECK=OFF`)
to keep image build times predictable.

## Future Work

Tracked in [#87](https://github.com/HomericIntelligence/ProjectCharybdis/issues/87):
evaluate whether a curated cppcheck suppression list would justify promoting cppcheck
findings to hard errors in a future iteration.
