# ProjectCharybdis Roadmap

ProjectCharybdis is the chaos and resilience testing harness for the HomericIntelligence
distributed agent mesh. This roadmap defines what must be true before each release and
where post-release improvements land.

GitHub milestones track the same scopes as the sections below:

- [v0.1.0 — Release Readiness](https://github.com/HomericIntelligence/ProjectCharybdis/milestone/1)
- [v0.2.0 — Hardening & Observability](https://github.com/HomericIntelligence/ProjectCharybdis/milestone/2)
- [Backlog](https://github.com/HomericIntelligence/ProjectCharybdis/milestone/3)

---

## v0.1.0 — Release Readiness (target: 2026-06-30)

A v0.1.0 tag is not cut until every item in this section is resolved. The goal is a
provably safe, reproducible, and peer-reviewed build.

### Security gates (non-negotiable)

- [ ] #8 Pin trivy-action to a SHA digest instead of `@master`
- [ ] #9 Set `exit-code: '1'` in Trivy so CRITICAL/HIGH findings block CI
- [ ] #20 Integrate CodeQL SAST into the CI pipeline
- [ ] #21 Add `USER` directive to Dockerfile — do not run as root

### Safety & correctness

- [ ] #7 Implement `ENABLE_SANITIZERS` — wire `-fsanitize=address,undefined` in CMake
- [ ] #10 Make sanitizer presets functional (ASAN/TSan/UBSan absent despite presets)
- [ ] #22 Guard `std::stoi()` port parse in `HttpTestClient` constructor
- [ ] #23 Enforce response body size limit to prevent OOM in `HttpTestClient`

### Reproducible builds

- [ ] #18 Commit `conan.lock` so Conan dependency versions are pinned
- [ ] #19 Remove `pixi.lock` from `.gitignore` — commit tool version lock

### CI hygiene

- [ ] #15 Require at least one approving review before merge
- [ ] #17 Extract Conan install block into a reusable composite action (copy-pasted 5×)

### Minimum documentation

- [ ] #28 Expand `README.md` — add env vars, prerequisites, architecture overview
- [ ] #29 Fix `CONTRIBUTING.md` — replace FetchContent references with Conan/pixi
- [ ] #30 *(this issue)* Define milestones and publish this roadmap

---

## v0.2.0 — Hardening & Observability (target: 2026-09-30)

Post-release work that raises quality from functional to production-grade.

### Testing completeness

- [ ] #13 Run integration tests in CI with real NATS + Agamemnon service containers
- [ ] #14 Add chaos resilience assertions — validate fault effects, not just fault injection
- [ ] #56 CodeQL SAST job wired into CI (follow-up after #20 scaffolding)

### Sanitizer CI matrix

- [ ] #60 Wire ASAN preset into `build-test.yml` matrix
- [ ] #61 Document or add ASAN/TSan to typecheck job
- [ ] #73 Add MSan preset for Clang-only builds
- [ ] #74 Fail CI fast on first UBSan error
- [ ] #75 Populate `tsan.supp` as TSan CI runs surface false positives

### Container & release pipeline

- [ ] #25 Add multi-stage Dockerfile with a deployable runtime stage
- [ ] #26 Publish container image in CI and test it
- [ ] #27 Add release workflow: git tags, `CHANGELOG.md`, binary artifacts
- [ ] #16 Ship a deployment/release pipeline (CI currently stops at build+test)

### Code quality

- [ ] #11 Refactor `HttpTestClient` — one persistent client, shared timeout constants
- [ ] #12 Enable `clang-tidy` `WarningsAsErrors` for the check categories that matter

### AI agent tooling

- [ ] #24 Write `AGENTS.md` — document multi-agent coordination, Nestor invocation path
- [ ] #31 Populate or remove `.claude/agents/` (currently an empty YAGNI placeholder)

### Security follow-ons

- [ ] #32 Add data privacy documentation for chaos test interactions
- [ ] #64 Pin gitleaks download URL in secrets-scan job
- [ ] #65 Automate SHA resolution in CI lint step
- [ ] #66 Document Dependabot PR review policy for SHA updates
- [ ] #68 Fix `gitleaks || true` — secrets scan must be able to fail CI
- [ ] #69 Pin Trivy action to a SHA instead of a mutable tag
- [ ] #70 Add `.trivyignore` with documented suppressions policy
- [ ] #71 Fix Conan audit `|| true` swallowing all errors

### Developer experience

- [ ] #58 Add MSan preset documentation
- [ ] #59 Populate `tsan.supp` as CI TSan runs reveal false positives
- [ ] #62 Document ASAN/TSan local workflow in README or CONTRIBUTING
- [ ] #76 Gate sanitizer presets on GCC/Clang version check in CMake
- [ ] #77 Document sanitizer presets in CLAUDE.md or README

---

## Backlog (no fixed date)

Minor and nitpick items picked up opportunistically.

- [ ] #33 Fix missing `#include <set>` in `test_protocol_correctness.cpp`
- [ ] #34 Remove `using namespace projectcharybdis` from integration test files
- [ ] #35 Deduplicate `agent_id` extraction logic in test
- [ ] #36 Remove dead Renovate FetchContent custom manager
- [ ] #37 Validate URL port before `std::stoi` call
- [ ] #38 Add timeout to mock server startup loop in unit test
- [ ] #39 Add retry/circuit-breaker logic to `HttpTestClient`
- [ ] #40 Add PR and issue templates
- [ ] #41 Add `.dockerignore` to trim Docker build context
- [ ] #42 Fix `scripts/lint.sh` silently suppressing cmake configure errors
- [ ] #43 Add `.editorconfig` for editor-level formatting
- [ ] #44 Add audit trail for chaos injection events
- [ ] #45 Remove `.claude/agents/.gitkeep` if agents directory stays empty
- [ ] #46 Replace magic timeout numbers with named constants
- [ ] #47 Replace trivial version tests in `test_main.cpp` with meaningful coverage
- [ ] #48 Make `just coverage` depend on `deps` like `build` does
- [ ] #49 Use distinct contact emails in `CODE_OF_CONDUCT.md` and `SECURITY.md`
