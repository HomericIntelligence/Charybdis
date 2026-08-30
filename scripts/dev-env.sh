#!/usr/bin/env bash
# PATH sanitizer for Charybdis build entrypoints (#164).
#
# Sourced (not executed) by the build recipes in `justfile` and by
# `scripts/test.sh`. Strips any conda/pixi environment bin directories from
# PATH so conan/cmake resolve the system compiler instead of a foreign
# conda-forge GCC whose C++ runtime ABI differs (the root cause of the
# undefined `__cxa_call_terminate` / `_M_replace_cold` / `str() const &`
# references from libgtest.a).
#
# Escape hatch: set CHARYBDIS_ALLOW_FOREIGN_ENV=1 to skip sanitization for
# intentional cross-environment work. Must be safe to source under `set -u`.
# See CONTRIBUTING.md ("Troubleshooting: GCC ABI / undefined reference from
# libgtest") for the full failure-mode description and recovery steps.

_charybdis_sanitize_path() {
	# Escape hatch first: leave the caller's PATH untouched when set.
	if [ "${CHARYBDIS_ALLOW_FOREIGN_ENV:-0}" = "1" ]; then
		return 0
	fi

	local cleaned=()
	local removed=()
	local entry
	local prefix_bin="${CONDA_PREFIX:-}/bin"

	local old_ifs="${IFS}"
	IFS=':'
	for entry in ${PATH:-}; do
		[ -n "${entry}" ] || continue
		case "${entry}" in
		*/envs/*/bin | */envs/bin | */conda/bin | */condabin | */miniconda*/bin | */anaconda*/bin | "${prefix_bin}")
			removed+=("${entry}")
			;;
		*)
			cleaned+=("${entry}")
			;;
		esac
	done
	IFS="${old_ifs}"

	if [ "${#removed[@]}" -eq 0 ]; then
		return 0
	fi

	PATH="$(IFS=':'; echo "${cleaned[*]:-/usr/local/bin:/usr/bin:/bin}")"
	export PATH

	{
		echo "WARNING: removed foreign conda/pixi entries from PATH:"
		local entry
		for entry in "${removed[@]}"; do
			echo "  ${entry}"
		done
		echo "The project must be built with the system toolchain; mixing a"
		echo "conda-forge GCC C++ runtime with the system compiler produces"
		echo "undefined-reference link errors from libgtest.a."
		echo "Set CHARYBDIS_ALLOW_FOREIGN_ENV=1 to bypass this sanitizer."
		echo "See CONTRIBUTING.md for details."
	} >&2
}

_charybdis_sanitize_path
unset -f _charybdis_sanitize_path
