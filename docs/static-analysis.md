# Static Analysis in Charybdis

## Where the tools run

`cmake/StaticAnalyzers.cmake` defines two opt-in tools:

- **clang-tidy** — via `CMAKE_CXX_CLANG_TIDY`
- **cppcheck** — via `CMAKE_CXX_CPPCHECK`

Both default to `OFF`. Each is enabled only by its own job in the
[Static Analysis workflow](.github/workflows/static-analysis.yml), which opts in explicitly:

```bash
cmake --preset debug -DCharybdis_ENABLE_CLANG_TIDY=ON   # clang-tidy job
cmake --preset debug -DCharybdis_ENABLE_CPPCHECK=ON     # cppcheck job
```

Those two jobs feed the required `lint` and `All Static Analysis Checks` aggregates.

### Why the defaults are OFF

Leaving them `ON` meant every configuration that did not explicitly opt out re-ran clang-tidy
over every translation unit — the `build-test` matrix (gcc/clang × debug/release) plus the
`install`, `asan`/`tsan`, `coverage`, `codeql`, `integration-tests` and `release` builds —
while the dedicated `clang-tidy` job already existed to do exactly that. See
[#373](https://github.com/HomericIntelligence/Charybdis/issues/373).

`scripts/test-static-analysis-policy.py` fails if either default flips back, if an analyzer is
enabled anywhere other than the Static Analysis workflow, or if the required aggregates stop
gating on both analyzer jobs.

The Docker release build opts both out explicitly
(`-DCharybdis_ENABLE_CLANG_TIDY=OFF -DCharybdis_ENABLE_CPPCHECK=OFF`) to keep image build times
predictable.

## clang-tidy

clang-tidy findings are treated as **build errors**. The `.clang-tidy` config enables
`WarningsAsErrors`, so any clang-tidy diagnostic fails the build immediately, which makes the
`clang-tidy` job a hard gate.

## cppcheck

cppcheck runs with `--enable=all --inconclusive --inline-suppr` and is **advisory-only by
design**:

- cppcheck findings do **not** fail the build. CMake's `CMAKE_CXX_CPPCHECK` integration passes
  the command through verbatim and does not add `--error-exitcode`, so cppcheck exits `0` and
  the finding is reported without failing the build.
- Consequently the `cppcheck` job must complete, but CI will not go red from a cppcheck finding
  alone. Developers should review that job's output and address genuine issues.

## Rationale for Keeping cppcheck Advisory

cppcheck's `--inconclusive` mode produces a higher false-positive rate than clang-tidy.
Promoting cppcheck to a hard error would require a curated suppression list and regular
maintenance; that posture was settled in
[#87](https://github.com/HomericIntelligence/Charybdis/issues/87). Until that investment is
made, advisory mode is the appropriate posture.

Note that the gate would have to be added explicitly: passing `--error-exitcode=1` makes
cppcheck exit non-zero on *any* finding, style-only ones included, so promoting it to a hard
error is a whole-codebase decision rather than a flag flip.

## Enabling a Tool Locally

Either tool can be enabled per build without touching the source:

```bash
cmake --preset debug \
  -DCharybdis_ENABLE_CLANG_TIDY=ON  # clang-tidy only
```

`scripts/lint.sh` configures with clang-tidy disabled and then drives clang-tidy directly
against `build/debug/compile_commands.json`, so `just lint` analyses each file once rather
than twice.
