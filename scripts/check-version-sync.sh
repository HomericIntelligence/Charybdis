#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

CMAKE_VERSION=$(grep -A5 'project(' "${ROOT}/CMakeLists.txt" \
  | grep -oP '\d+\.\d+\.\d+' | head -1)

if [ -z "${CMAKE_VERSION}" ]; then
  echo "ERROR: Could not parse VERSION from CMakeLists.txt" >&2
  exit 1
fi
echo "Canonical version: ${CMAKE_VERSION}"

FAIL=0

if [ -f "${ROOT}/conanfile.py" ]; then
  CONAN_VERSION=$(grep -m1 '^\s*version\s*=' "${ROOT}/conanfile.py" \
    | grep -oP '\d+\.\d+\.\d+' | head -1)
  if [ -n "${CONAN_VERSION}" ] && [ "${CONAN_VERSION}" != "${CMAKE_VERSION}" ]; then
    echo "ERROR: conanfile.py version (${CONAN_VERSION}) does not match CMakeLists.txt (${CMAKE_VERSION})" >&2
    FAIL=1
  else
    echo "conanfile.py: ${CONAN_VERSION:-N/A} OK"
  fi
fi

if [ -f "${ROOT}/pixi.toml" ]; then
  PIXI_VERSION=$(grep -m1 '^version' "${ROOT}/pixi.toml" \
    | grep -oP '\d+\.\d+\.\d+' | head -1)
  if [ -n "${PIXI_VERSION}" ] && [ "${PIXI_VERSION}" != "${CMAKE_VERSION}" ]; then
    echo "ERROR: pixi.toml version (${PIXI_VERSION}) does not match CMakeLists.txt (${CMAKE_VERSION})" >&2
    FAIL=1
  else
    echo "pixi.toml: ${PIXI_VERSION:-N/A} OK"
  fi
fi

exit "${FAIL}"
