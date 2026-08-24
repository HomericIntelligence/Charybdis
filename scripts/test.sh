#!/usr/bin/env bash
set -euo pipefail
# Sanitize PATH of foreign conda/pixi envs so cmake resolves the system
# compiler (GCC ABI mismatch causes undefined references from libgtest.a,
# see CONTRIBUTING.md and issue #164).
. "$(dirname "$0")/dev-env.sh"
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure "$@"
