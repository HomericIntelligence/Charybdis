#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
if ! cmake --preset debug -DCharybdis_ENABLE_CLANG_TIDY=OFF >/dev/null; then
  echo "lint.sh: cmake configure failed (preset=debug). Re-run without redirect for details:" >&2
  echo "  cmake --preset debug -DCharybdis_ENABLE_CLANG_TIDY=OFF" >&2
  exit 1
fi
# No --config-file is passed deliberately: clang-tidy's parent-directory
# config discovery applies test/.clang-tidy (which disables GTest fixture
# member-visibility checks) to files under test/, while the root
# .clang-tidy applies to include/ and src/.
find "${ROOT_DIR}/include" "${ROOT_DIR}/src" "${ROOT_DIR}/test" \
  -name "*.cpp" -o -name "*.hpp" | \
  xargs clang-tidy -p "${ROOT_DIR}/build/debug" "$@"
