#!/usr/bin/env bash
# Drive cmake/Sanitizers.cmake under controlled fake-compiler state.
# Each case generates a minimal CMakeLists.txt that sets
# CMAKE_CXX_COMPILER_ID / CMAKE_CXX_COMPILER_VERSION *before* including the
# module (no project() call, so CMake's compiler detection cannot overwrite
# them) and asserts on cmake's exit code and stderr.
#
# A plain `cmake -P` script is NOT used here because add_compile_options /
# add_link_options are not scriptable commands.
set -u
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SANITIZERS_MODULE="${REPO_ROOT}/cmake/Sanitizers.cmake"
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT
FAIL=0

run_case() {
  local name="$1" compiler_id="$2" compiler_ver="$3" enable_var="$4" \
        expect_exit="$5" expect_pattern="$6"
  local src="${TMPDIR}/${name}"
  mkdir -p "$src"
  cat > "${src}/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.20)
set(PROJECT_NAME Charybdis)
set(CMAKE_CXX_COMPILER_ID "${compiler_id}")
set(CMAKE_CXX_COMPILER_VERSION "${compiler_ver}")
set(Charybdis_${enable_var} ON)
include("${SANITIZERS_MODULE}")
EOF
  local out
  out="$(cmake -S "$src" -B "${src}/build" 2>&1)"
  local rc=$?
  # Normalize line wrapping so patterns survive CMake's ~76-column message wrap.
  local flat
  flat="$(tr '\n' ' ' <<<"$out")"
  if [[ "$rc" -ne "$expect_exit" ]]; then
    echo "FAIL [$name]: expected exit $expect_exit, got $rc"
    echo "$out"
    FAIL=1
    return
  fi
  if [[ -n "$expect_pattern" ]] && ! grep -E -q "$expect_pattern" <<<"$flat"; then
    echo "FAIL [$name]: stderr did not match /$expect_pattern/"
    echo "$out"
    FAIL=1
    return
  fi
  echo "PASS [$name]"
}

# Positive: guard fires on old GCC under ENABLE_SANITIZERS.
run_case asan_oldgcc   GNU   8.4.0   ENABLE_SANITIZERS 1 "GCC >= 9.*8\.4\.0"
# Positive: guard fires on old GCC under ENABLE_TSAN.
run_case tsan_oldgcc   GNU   8.4.0   ENABLE_TSAN       1 "ThreadSanitizer.*GCC >= 9"
# Positive: guard fires on old Clang under ENABLE_SANITIZERS.
run_case asan_oldclang Clang 5.0.0   ENABLE_SANITIZERS 1 "Clang >= 6.*5\.0\.0"
# Positive: guard fires on old Clang under ENABLE_TSAN.
run_case tsan_oldclang Clang 5.0.0   ENABLE_TSAN       1 "ThreadSanitizer.*Clang >= 6"
# Negative: known-good GCC passes.
run_case asan_newgcc   GNU   11.4.0  ENABLE_SANITIZERS 0 ""
# Negative: known-good Clang passes.
run_case tsan_newclang Clang 14.0.0  ENABLE_TSAN       0 ""
# Edge: boundary versions pass.
run_case asan_boundary_gcc   GNU   9.0.0 ENABLE_SANITIZERS 0 ""
run_case asan_boundary_clang Clang 6.0.0 ENABLE_SANITIZERS 0 ""
# Edge: empty version string -> WARNING, not FATAL_ERROR (exit 0).
run_case empty_version GNU   ""      ENABLE_SANITIZERS 0 "could not be detected"

exit "$FAIL"
