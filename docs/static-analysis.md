# Static Analysis in Charybdis

## Tools Configured

`cmake/StaticAnalyzers.cmake` wires up two static analysis tools:

- **clang-tidy** — via `CMAKE_CXX_CLANG_TIDY`
- **cppcheck** — via `CMAKE_CXX_CPPCHECK`

Both default to **`OFF`** and are enabled only by their own job in
`.github/workflows/static-analysis.yml`, which opts in with
`-DCharybdis_ENABLE_<TOOL>=ON`. Defaulting them `ON` meant every configuration that did not
explicitly opt out re-ran them per translation unit — see #373. Because the analyzers are
opt-in, a local `cmake --preset debug` does **not** run them; join the container or install
the tool and pass the flag deliberately when you want them.

Both jobs are gated by the required `lint` and `All Static Analysis Checks` aggregates.

## clang-tidy

clang-tidy findings are **build errors**. The `.clang-tidy` config enables
`WarningsAsErrors`, so any diagnostic fails the build immediately.

## cppcheck

cppcheck findings are also **build errors**. `--error-exitcode=1` is passed explicitly,
which is what makes that true: CMake's `CMAKE_CXX_CPPCHECK` integration never adds the flag
itself, so without it the build succeeds even on an `error:`-severity finding and the
required job passes while reporting nothing fatal. See #375.

cppcheck runs with `--enable=all --inline-suppr --inconclusive`. Note that
`--error-exitcode=1` trips on **any** reported finding, `information` severity included, so
the severity name is not a hint about what the gate tolerates.

### gtest translation units

`--library=googletest` is load-bearing. cppcheck's shipped `googletest.cfg` defines `TEST`,
`TEST_F` and `TEST_P` as function definitions, which is the only way cppcheck can parse
them. Without it `<gtest/gtest.h>` is never on cppcheck's include path, `TEST` is an unknown
macro, and cppcheck aborts each gtest file at the *second* `TEST()` with
`error: syntax error [syntaxError]` — analyzing none of it. That silently skipped 11 of the
13 translation units in this repository, which is why the gate added in #375 also had to
turn analysis back on.

### The suppression baseline

Three ids are suppressed globally, each because it cannot work under CMake's
per-translation-unit invocation:

| id | why it cannot work here |
| --- | --- |
| `missingIncludeSystem` | cppcheck is given no `-isystem` paths, so every `#include <...>` reports it. cppcheck's own message states it does not need standard library headers. |
| `unusedFunction` | Only reliable in whole-program mode. Per TU, every function used from another translation unit is a false positive, and `googletest.cfg` turns each `TEST` body into a standalone function. |
| `unmatchedSuppression` | A suppression is "unused" in any TU with no finding of that id (for example `unusedFunction` in `main.cpp`), which itself reports as a finding. Without this the gate would fail on the very suppressions that make it usable. |

`missingInclude` is deliberately **not** suppressed. It covers *project* headers, and the
build passes `-I include` and `-I build/debug/include`, so those resolve. It is a different
id from `missingIncludeSystem`, and the pre-#375 argument list conflated the two — the
suppression matched nothing and produced 13 `unmatchedSuppression` findings instead.

Because `--suppress=unmatchedSuppression` hides stale suppressions, the exact list above is
pinned by `scripts/test-static-analysis-policy.py`, which fails if an entry is added,
removed or renamed.

### Suppressing a one-off finding

Everything outside that list is either fixed or suppressed **inline**, so the exception sits
next to the code it applies to:

```cpp
// cppcheck-suppress unusedScopedObject -- the temporary IS the expression under test;
// EXPECT_THROW expands to try{code;}catch(e){}, discarding the nameable object.
EXPECT_THROW(HttpTestClient("http://127.0.0.1:99999"), std::runtime_error);
```

`--inline-suppr` is required for these to take effect. Prefer a targeted inline suppression
over widening the global list: the global list is pinned, and a new entry means the gate is
no longer evaluated on that class of finding anywhere.

## Opting Out Per Build

Each tool can be disabled without touching the source:

```bash
cmake --preset debug \
  -DCharybdis_ENABLE_CPPCHECK=OFF   # disable cppcheck only
  -DCharybdis_ENABLE_CLANG_TIDY=OFF # disable clang-tidy only
```

The Docker release build disables both (`Dockerfile:114-115`) to keep image build times
predictable, and `scripts/lint.sh` configures with `-DCharybdis_ENABLE_CLANG_TIDY=OFF`
before driving `clang-tidy` itself.

## Reproducing CI Locally

CI installs cppcheck from `ubuntu-24.04` apt, which is **2.13.0**. Behaviour — especially
which ids fire and whether `--error-exitcode` triggers — is version specific, so match it:

```bash
podman run --rm -v "$PWD:/w" -w /w ubuntu:24.04 bash -lc '
  apt-get update -qq && apt-get install -y -qq cppcheck >/dev/null
  cppcheck --version   # 2.13.0
'
```

Then run the workflow's two steps: `cmake --preset debug -DCharybdis_ENABLE_CPPCHECK=ON`
followed by `cmake --build --preset debug`.
