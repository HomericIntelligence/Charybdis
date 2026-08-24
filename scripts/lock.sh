#!/usr/bin/env bash
set -euo pipefail

# Regenerate conan.lock using the canonical GCC-14/Debug/Linux/x86_64 profile
# and show any diff against the committed lockfile.
#
# The GCC-14/Debug profile is the canonical generation profile because Conan 2.x
# lockfiles are dependency-graph-level: they pin recipe revisions, which are
# compiler-agnostic. The result is therefore valid for all compilers, including
# the Clang CI legs in build-test.yml. Never hand-generate a "Clang lockfile";
# CI's lockfile-integrity job asserts that a Clang-settings resolution matches
# this file byte-for-byte.

# Canonical CI profile. .github/workflows/lock-check.yml runs on
# ubuntu-24.04 (Linux/x86_64); a lockfile generated against any other
# OS/arch will fail integrity verification on CI.
CANONICAL_OS=Linux
CANONICAL_ARCH=x86_64

FORCE=0
for arg in "$@"; do
  case "$arg" in
    --force)   FORCE=1 ;;
    -h|--help) echo "usage: $0 [--force]"; exit 0 ;;
    *)         echo "error: unknown arg: $arg" >&2
               echo "usage: $0 [--force]" >&2
               exit 2 ;;
  esac
done

HOST_OS="$(uname -s)"
HOST_ARCH="$(uname -m)"

if [[ "$HOST_OS" != "$CANONICAL_OS" || "$HOST_ARCH" != "$CANONICAL_ARCH" ]]; then
  msg="host ${HOST_OS}/${HOST_ARCH} does not match canonical CI profile ${CANONICAL_OS}/${CANONICAL_ARCH}"
  if [[ "$FORCE" -eq 1 ]]; then
    {
      echo "================================================================"
      echo "WARNING: $msg"
      echo "Regenerating conan.lock against the HOST profile under --force."
      echo "The resulting lockfile will NOT pass CI lock-check."
      echo "DO NOT commit the resulting conan.lock."
      echo "To produce a CI-valid lockfile, re-run inside a Linux/x86_64"
      echo "environment with conan + gcc-14 installed (same toolchain as"
      echo ".github/workflows/lock-check.yml)."
      echo "================================================================"
    } >&2
    # Drop ALL host-specific settings (os, arch, compiler family) under
    # --force; conan cannot cross-resolve to gcc-14/Linux/x86_64 from a
    # macOS/ARM host without a matching cross-profile. Keep only portable
    # choices so the local invocation actually produces a lockfile.
    conan profile detect --force
    conan lock create . \
      -s compiler.cppstd=20 \
      -s build_type=Debug
    exit 0
  fi
  {
    echo "error: $msg"
    echo "Regenerating conan.lock here would break CI lock-check."
    echo "Run this inside a Linux/x86_64 environment with conan + gcc-14"
    echo "installed (same toolchain as .github/workflows/lock-check.yml)."
    echo "Or pass --force to regenerate a HOST-profile lockfile for local"
    echo "inspection only (must not be committed)."
  } >&2
  exit 1
fi

conan profile detect --force

conan lock create . \
  -s compiler=gcc \
  -s compiler.version=14 \
  -s compiler.libcxx=libstdc++11 \
  -s compiler.cppstd=20 \
  -s build_type=Debug \
  -s os="$CANONICAL_OS" \
  -s arch="$CANONICAL_ARCH"

git diff conan.lock
