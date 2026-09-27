#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
if ! cmake --preset debug -DCharybdis_ENABLE_CLANG_TIDY=OFF >/dev/null; then
  echo "lint.sh: cmake configure failed (preset=debug). Re-run without redirect for details:" >&2
  echo "  cmake --preset debug -DCharybdis_ENABLE_CLANG_TIDY=OFF" >&2
  exit 1
fi

# Mirror the in-build clang-tidy header resolution (cmake/StaticAnalyzers.cmake):
# derive the builtin include dir and sysroot from the live compiler so clang-tidy
# can resolve compiler-builtin headers (stddef.h) and libc headers even when its
# LLVM frontend differs from the compile-toolchain frontend (conda/pixi GCC).
COMPILER="${CXX:-g++}"
GCC_INCLUDE_DIR="$("$COMPILER" -print-file-name=include)" || true
COMPILER_SYSROOT="$("$COMPILER" -print-sysroot)" || true
EXTRA_ARGS=()
if [ -n "${GCC_INCLUDE_DIR}" ] && [ "${GCC_INCLUDE_DIR}" != "include" ]; then
  EXTRA_ARGS+=("--extra-arg=-isystem${GCC_INCLUDE_DIR}")
fi
if [ -n "${COMPILER_SYSROOT}" ]; then
  EXTRA_ARGS+=("--extra-arg=--sysroot=${COMPILER_SYSROOT}")
fi
find "${ROOT_DIR}/include" "${ROOT_DIR}/src" "${ROOT_DIR}/test" \
  -name "*.cpp" -o -name "*.hpp" | \
  xargs clang-tidy -p "${ROOT_DIR}/build/debug" --config-file="${ROOT_DIR}/.clang-tidy" \
    "${EXTRA_ARGS[@]}" "$@"
