# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- `feat(build)`: migrate from pixi to uv for the build toolchain (Odysseus
  ADR-018, mirroring Agamemnon #457). CMake/Ninja/Conan/gcovr/pre-commit are now
  uv-managed locked PyPI wheels (`pyproject.toml` + `uv.lock`); the C++ compiler
  and clang-tidy/clang-format come from the system (apt). The Lock Check and
  `deps/version-sync` jobs switch from pixi to `uv lock --check` /
  `pyproject.toml`, and the Dockerfile builder pulls uv via a digest-pinned
  `COPY --from=uv` named stage. All required check-run names are preserved.

## [0.1.0] — 2026-05-04

### Added

- Initial project scaffolding: CMake build system, Conan package management, Pixi environment
- Chaos API client stubs for network-partition, latency, kill, queue-starve, and DELETE endpoints
- CI workflows: build/test, static analysis, code coverage, and required-checks fan-in
- Version constants via generated `version.hpp` (from CMake `configure_file`)
- `CHANGELOG.md` and release workflow automation
