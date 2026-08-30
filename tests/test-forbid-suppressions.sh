#!/usr/bin/env bash
# shellcheck shell=bash disable=SC2016  # fixtures intentionally quote $( ) literally
# Regression test for the forbid-suppressions CI gate and the forbid-or-true
# pre-commit hook (issue #247, PR #362 review thread PRRT_kwDORzkfKs6b5jwP).
#
# Verifies that the shared "no silent || true" pattern:
#   1. is extracted as a single source of truth from scripts/run_ci_local.sh,
#   2. is byte-identical to the forbid-or-true pre-commit hook entry in
#      .pre-commit-config.yaml (local/CI lockstep), and
#   3. behaves correctly on positive fixtures (clean lines must pass) and
#      negative fixtures (suppressed-failure lines must be rejected, with a
#      correct file:line citation).
#
# Host-only: no container required. Exit nonzero on any failure.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

failures=0

fail() {
    printf 'FAIL: %s\n' "$1" >&2
    failures=$((failures + 1))
}

pass() {
    printf 'PASS: %s\n' "$1"
}

cd "${REPO_ROOT}"

# ============================================================================
# 1. Extract the pattern — single source of truth
# ============================================================================

pattern_ci="$(sed -n "/^pattern='/s/^pattern='\(.*\)'$/\1/p" scripts/run_ci_local.sh)"
if [ -z "${pattern_ci}" ]; then
    echo "ABORT: could not extract pattern= line from scripts/run_ci_local.sh" >&2
    exit 1
fi
if [ "$(printf '%s\n' "${pattern_ci}" | wc -l)" -ne 1 ]; then
    echo "ABORT: expected exactly one pattern= line in scripts/run_ci_local.sh" >&2
    exit 1
fi

# The pre-commit hook entry lives directly under id: forbid-or-true; pick the
# first quoted entry: line after that id so other hooks do not interfere.
pattern_hook="$(awk '/id: forbid-or-true/{found=1} found && /^ *entry: '"'"'/{sub(/^ *entry: '"'"'/,""); sub(/'"'"'.*$/,""); print; exit}' .pre-commit-config.yaml)"
if [ -z "${pattern_hook}" ]; then
    echo "ABORT: could not extract forbid-or-true entry regex from .pre-commit-config.yaml" >&2
    exit 1
fi

if [ "${pattern_ci}" != "${pattern_hook}" ]; then
    fail "pattern drift: run_ci_local.sh vs .pre-commit-config.yaml forbid-or-true differ"
    printf '  ci:   %s\n  hook: %s\n' "${pattern_ci}" "${pattern_hook}" >&2
else
    pass "extracted patterns are identical (local/CI lockstep): ${pattern_ci}"
fi

# ============================================================================
# 2. Build fixtures (temp dir, cleaned up via trap)
#
# NOTE: fixture strings containing the forbidden idiom are assembled by
# concatenation so that this file itself never contains a literal violating
# sequence (the forbid-or-true hook scans *.sh anywhere).
# ============================================================================

OROR='||'
TMPDIR_FIXTURES="$(mktemp -d)"
trap 'rm -rf "${TMPDIR_FIXTURES}"' EXIT

# --- Positive fixtures: clean, MUST NOT match ---
pos_dir="${TMPDIR_FIXTURES}/positive"
mkdir "${pos_dir}"
printf '%s\n' '# we used '"${OROR}"' true here once' > "${pos_dir}/comment.txt"
printf '%s\n' 'echo hello world' > "${pos_dir}/echo.txt"

# --- Negative fixtures: suppressed failures, MUST match with citation ---
neg_dir="${TMPDIR_FIXTURES}/negative"
mkdir "${neg_dir}"
printf '%s\n' "echo hi ${OROR} true" > "${neg_dir}/eol.txt"
printf '%s\n' 'foo "$(cmd '"${OROR}"' true)"' > "${neg_dir}/paren.txt"
printf '%s\n' 'run '"${OROR}"' true # best effort' > "${neg_dir}/comment_trail.txt"
printf '%s\n' 'a | b '"${OROR}"' true; c' > "${neg_dir}/semicolon.txt"

# ============================================================================
# 3. Positive fixtures must be clean (grep rc == 1)
# ============================================================================

for f in "${pos_dir}"/*; do
    name="$(basename "${f}")"
    rc=0
    grep_out="$(grep -HnP -- "${pattern_ci}" "${f}")" || rc=$?
    if [ "${rc}" -eq 0 ]; then
        fail "positive fixture rejected (should pass): ${name} -> ${grep_out}"
    elif [ "${rc}" -eq 1 ]; then
        pass "positive fixture passes: ${name}"
    else
        fail "grep exited ${rc} scanning positive fixture ${name}"
    fi
done

# ============================================================================
# 4. Negative fixtures must be rejected (grep rc == 0, file:line cited)
# ============================================================================

declare -A neg_expect=(
    ["eol.txt"]="echo hi"
    ["paren.txt"]='foo "$(cmd'
    ["comment_trail.txt"]="run "
    ["semicolon.txt"]="a | b "
)

for f in "${neg_dir}"/*; do
    name="$(basename "${f}")"
    rc=0
    grep_out="$(grep -HnP -- "${pattern_ci}" "${f}")" || rc=$?
    if [ "${rc}" -ne 0 ]; then
        fail "negative fixture NOT rejected (rc=${rc}): ${name}"
        continue
    fi
    if ! printf '%s\n' "${grep_out}" | grep -q "^${f}:1:"; then
        fail "negative fixture ${name} not cited as \${file}:1 — got: ${grep_out}"
        continue
    fi
    if ! printf '%s\n' "${grep_out}" | grep -qF "${neg_expect[${name}]}"; then
        fail "negative fixture ${name} cited but content unexpected — got: ${grep_out}"
        continue
    fi
    pass "negative fixture rejected with citation: ${name} -> ${grep_out}"
done

# ============================================================================
# 5. Summary
# ============================================================================

echo ""
if [ "${failures}" -gt 0 ]; then
    printf 'forbid-suppressions regression test: FAILED (%d failure(s))\n' "${failures}"
    exit 1
fi
echo 'forbid-suppressions regression test: PASSED'
