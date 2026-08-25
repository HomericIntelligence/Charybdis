set shell := ["bash", "-c"]

default:
  @just --list

# Install Conan dependencies (gtest). Conan/CMake/Ninja are uv-managed PyPI
# wheels (ADR-018), so recipes invoke them via `uv run`.
deps:
  uv run conan install . --output-folder=build/debug --profile=conan/profiles/debug --build=missing

# Install Conan dependencies for release
deps-release:
  uv run conan install . --output-folder=build/release --profile=conan/profiles/default --build=missing

build: deps
  uv run cmake --preset debug && uv run cmake --build --preset debug

# Pass AGAMEMNON_URL and NATS_URL through to the test process so developers
# can point integration tests at custom endpoints via their shell env. CTest
# inherits the parent environment, but listing the vars here makes the
# contract explicit (and gives `just --evaluate` something to surface).
test AGAMEMNON_URL=env_var_or_default("AGAMEMNON_URL", "") NATS_URL=env_var_or_default("NATS_URL", ""):
  #!/usr/bin/env bash
  set -euo pipefail
  export AGAMEMNON_URL="{{AGAMEMNON_URL}}"
  export NATS_URL="{{NATS_URL}}"
  uv run ctest --preset debug --output-on-failure

# Standalone smoke tests for scripts/mock-agamemnon.py (no conan/cmake needed)
test-mock:
  python3 scripts/test-mock-agamemnon.py

lint:
  ./scripts/lint.sh

# Verify every `uses:` in workflows/composite-actions is SHA-pinned (#65)
check-action-pins:
  ./scripts/check-action-pins.sh

# Verify the Docker builder installs into its WORKDIR, not a root-pre-created path (#248)
check-docker-install-prefix:
  ./scripts/check-docker-install-prefix.sh

format:
  ./scripts/format.sh

format-check:
  ./scripts/format.sh --check

coverage: deps
  uv run cmake --preset coverage && uv run cmake --build --preset coverage && ./scripts/coverage.sh

merge-queue-policy:
  ./scripts/test-merge-queue-policy.py

clean:
  rm -rf build install

ci: merge-queue-policy
  uv run cmake --preset ci && uv run cmake --build --preset ci && uv run ctest --preset ci

# === Containerized CI (podman by default) ===

# Build the CI container image (podman first, docker fallback)
ci-build:
    podman build --ignorefile ci/.dockerignore -f ci/Containerfile -t charybdis-ci:local . || docker build -f ci/Containerfile -t charybdis-ci:local .

# Run CI lint checks in container
ci-lint:
    ./scripts/run_ci_local.sh lint

# Run CI markdownlint checks in container
ci-markdownlint:
    ./scripts/run_ci_local.sh markdownlint

# Run CI unit-tests checks in container
ci-unit-tests:
    ./scripts/run_ci_local.sh unit-tests

# Run CI integration-tests checks in container
ci-integration-tests:
    ./scripts/run_ci_local.sh integration-tests

# Run CI schema-validation checks in container
ci-schema-validation:
    ./scripts/run_ci_local.sh schema-validation

# Run CI security-secrets-scan checks in container
ci-security-secrets-scan:
    ./scripts/run_ci_local.sh security-secrets-scan

# Run CI deps-version-sync checks in container
ci-deps-version-sync:
    ./scripts/run_ci_local.sh deps-version-sync

# Run CI forbid-suppressions checks in container
ci-forbid-suppressions:
    ./scripts/run_ci_local.sh forbid-suppressions

# Run CI justfile-check checks in container
ci-justfile-check:
    ./scripts/run_ci_local.sh justfile-check

# Run CI symlink-check checks in container
ci-symlink-check:
    ./scripts/run_ci_local.sh symlink-check

# Run all CI checks in container
ci-all:
    ./scripts/run_ci_local.sh all
