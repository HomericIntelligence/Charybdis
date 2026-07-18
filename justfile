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
