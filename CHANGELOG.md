# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- `chore(iwyu)`: add missing explicit `#include` directives surfaced by an
  include-what-you-use audit of all `src/` and `test/src` translation units
  and the public headers (`<cstddef>` in `http_test_client.hpp`,
  `<string_view>` in `main.cpp`, `<memory>`/`<system_error>`/`<tuple>` in test
  sources) so no code relies on transitive standard-library inclusion (#183).

### Changed

- `ci(security)`: run `conan audit scan` (lockfile-driven, `-sl 7.0`) in the
  `security/dependency-scan` job with no `|| true` suppression — the only skip
  paths are the `conan audit --help` subcommand presence probe and a missing
  `CONAN_AUDIT_TOKEN` (both emit `::notice::` and exit 0) (#71). The
  ConanCenter provider token, when present, is injected from the
  `CONAN_AUDIT_TOKEN` repository secret via
  `CONAN_AUDIT_PROVIDER_TOKEN_CONANCENTER`; register at
  <https://conan.io/audit/register> to provision it.
- `feat(build)`: migrate from pixi to uv for the build toolchain (Odysseus
  ADR-018, mirroring Agamemnon #457). CMake/Ninja/Conan/gcovr/pre-commit are now
  uv-managed locked PyPI wheels (`pyproject.toml` + `uv.lock`); the C++ compiler
  and clang-tidy/clang-format come from the system (apt). The Lock Check and
  `deps/version-sync` jobs switch from pixi to `uv lock --check` /
  `pyproject.toml`, and the Dockerfile builder pulls uv via a digest-pinned
  `COPY --from=uv` named stage. All required check-run names are preserved.

- `tsan.supp` reset to an empty scaffold; speculative moodycamel
  suppressions removed (moodycamel is not a dependency). Added
  `docs/tsan-triage.md` runbook for adding TSan suppressions in response
  to real CI failures (#59).

## [0.1.0] — 2026-05-04

### Added

- Initial project scaffolding: CMake build system, Conan package management, Pixi environment
- Chaos API client stubs for network-partition, latency, kill, queue-starve, and DELETE endpoints
- CI workflows: build/test, static analysis, code coverage, and required-checks fan-in
- Version constants via generated `version.hpp` (from CMake `configure_file`)
- `CHANGELOG.md` and release workflow automation
