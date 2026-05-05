# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] — 2026-05-04

### Added
- Initial project scaffolding: CMake build system, Conan package management, Pixi environment
- Chaos API client stubs for network-partition, latency, kill, queue-starve, and DELETE endpoints
- CI workflows: build/test, static analysis, code coverage, and required-checks fan-in
- Version constants via generated `version.hpp` (from CMake `configure_file`)
- `CHANGELOG.md` and release workflow automation
