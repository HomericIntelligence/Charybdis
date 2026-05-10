#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
if ! cmake --preset debug -DProjectCharybdis_ENABLE_CLANG_TIDY=OFF >/dev/null; then
  echo "lint.sh: cmake configure failed (preset=debug). Re-run without redirect for details:" >&2
  echo "  cmake --preset debug -DProjectCharybdis_ENABLE_CLANG_TIDY=OFF" >&2
  exit 1
fi
find "${ROOT_DIR}/include" "${ROOT_DIR}/src" "${ROOT_DIR}/test" \
  -name "*.cpp" -o -name "*.hpp" | \
  xargs clang-tidy -p "${ROOT_DIR}/build/debug" --config-file="${ROOT_DIR}/.clang-tidy" "$@"
