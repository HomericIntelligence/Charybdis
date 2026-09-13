#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
# cmake/StaticAnalyzers.cmake FATAL_ERRORs during configure when
# Charybdis_ENABLE_IWYU=ON and include-what-you-use is not found, so a failed
# find_program aborts the configure step below automatically.
echo "check-iwyu.sh: configuring (preset=debug, IWYU=ON)..."
if ! CC=clang CXX=clang++ cmake --preset debug -DCharybdis_ENABLE_IWYU=ON; then
  echo "check-iwyu.sh: FAILED — cmake configure aborted (IWYU missing or configure error)." >&2
  exit 1
fi
echo "check-iwyu.sh: building (preset=debug)..."
if ! cmake --build --preset debug; then
  echo "check-iwyu.sh: FAILED — IWYU violations or build errors detected." >&2
  exit 1
fi
echo "check-iwyu.sh: OK — build completed with include-what-you-use enabled."
