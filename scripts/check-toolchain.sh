#!/usr/bin/env bash
# Preflight toolchain-consistency check (#164).
#
# Compares the GCC major version of the active C++ compiler against the
# `compiler.version` declared in a Conan profile. A mismatch means the Conan
# package IDs no longer describe the actual compiler, which can silently
# reuse gtest binaries built by a different ABI (see CONTRIBUTING.md).
#
# Usage: scripts/check-toolchain.sh [profile]
#   profile  Conan profile to check (default: conan/profiles/debug)
#
# Exit codes: 0 = consistent, 1 = mismatch or unreadable inputs.
set -euo pipefail

usage() {
	echo "usage: $0 [conan-profile]" >&2
	exit 1
}

[ "$#" -le 1 ] || usage

profile="${1:-conan/profiles/debug}"
cxx="${CXX:-g++}"

if [ ! -f "${profile}" ]; then
	echo "ERROR: Conan profile not found: ${profile}" >&2
	exit 1
fi

if ! command -v "${cxx}" >/dev/null 2>&1; then
	echo "ERROR: C++ compiler not found: ${cxx}" >&2
	exit 1
fi

actual="$("$cxx" -dumpfullversion -dumpversion 2>/dev/null || "$cxx" -dumpversion)"
actual_major="${actual%%.*}"

declared="$(sed -n 's/^compiler\.version=//p' "${profile}")"
if [ -z "${declared}" ]; then
	echo "ERROR: no 'compiler.version=' in ${profile}" >&2
	exit 1
fi
declared_major="${declared%%.*}"

if [ "${actual_major}" != "${declared_major}" ]; then
	{
		echo "ERROR: toolchain mismatch (GCC ABI link-failure risk, see #164):"
		echo "  ${cxx} reports major ${actual_major} (${actual})"
		echo "  ${profile} declares compiler.version=${declared}"
		echo ""
		echo "Conan keys cached packages on the *declared* settings, so stale"
		echo "gtest binaries built by another compiler may be reused and fail"
		echo "to link with undefined references (__cxa_call_terminate,"
		echo "_M_replace_cold, std::string::str() const&)."
		echo ""
		echo "Recovery:"
		echo "  just clean-deps && just build"
		echo "Or update the profile's compiler.version to match your compiler."
	} >&2
	exit 1
fi

exit 0
