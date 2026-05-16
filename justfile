set shell := ["bash", "-c"]

default:
  @just --list

# Install Conan dependencies (gtest)
deps:
  conan install . --output-folder=build/debug --profile=conan/profiles/debug --build=missing

# Install Conan dependencies for release
deps-release:
  conan install . --output-folder=build/release --profile=conan/profiles/default --build=missing

build: deps
  cmake --preset debug && cmake --build --preset debug

# Pass AGAMEMNON_URL and NATS_URL through to the test process so developers
# can point integration tests at custom endpoints via their shell env. CTest
# inherits the parent environment, but listing the vars here makes the
# contract explicit (and gives `just --evaluate` something to surface).
test AGAMEMNON_URL=env_var_or_default("AGAMEMNON_URL", "") NATS_URL=env_var_or_default("NATS_URL", ""):
  #!/usr/bin/env bash
  set -euo pipefail
  export AGAMEMNON_URL="{{AGAMEMNON_URL}}"
  export NATS_URL="{{NATS_URL}}"
  ctest --preset debug --output-on-failure

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
  cmake --preset coverage && cmake --build --preset coverage && ./scripts/coverage.sh

clean:
  rm -rf build install

ci:
  cmake --preset ci && cmake --build --preset ci && ctest --preset ci
