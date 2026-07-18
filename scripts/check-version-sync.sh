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

# ADR-018: pixi.toml replaced by pyproject.toml (uv-managed build toolchain).
if [ -f "${ROOT}/pyproject.toml" ]; then
  PY_VERSION=$(grep -m1 '^version' "${ROOT}/pyproject.toml" \
    | grep -oP '\d+\.\d+\.\d+' | head -1)
  if [ -n "${PY_VERSION}" ] && [ "${PY_VERSION}" != "${CMAKE_VERSION}" ]; then
    echo "ERROR: pyproject.toml version (${PY_VERSION}) does not match CMakeLists.txt (${CMAKE_VERSION})" >&2
    FAIL=1
  else
    echo "pyproject.toml: ${PY_VERSION:-N/A} OK"
  fi
fi

exit "${FAIL}"
