#!/usr/bin/env bash
set -euo pipefail

# Regenerate conan.lock using the canonical GCC-14/Debug/Linux/x86_64 profile
# and show any diff against the committed lockfile.

conan profile detect --force

conan lock create . \
  -s compiler=gcc \
  -s compiler.version=14 \
  -s compiler.libcxx=libstdc++11 \
  -s compiler.cppstd=20 \
  -s build_type=Debug \
  -s os=Linux \
  -s arch=x86_64

git diff conan.lock
