#!/usr/bin/env bash
set -euo pipefail

# Test harness for scripts/lock.sh host OS/arch guard (issue #118).
# Uses shim `uname` and `conan` executables on PATH to simulate hosts.

SCRIPT_UNDER_TEST="${1:?usage: test_lock_guard.sh <path-to-lock.sh>}"

SHIM_DIR="$(mktemp -d)"
trap 'rm -rf "$SHIM_DIR"' EXIT

ARGS_FILE="${SHIM_DIR}/conan.args"
: >"$ARGS_FILE"

cat >"${SHIM_DIR}/uname" <<'EOF'
#!/bin/sh
case "$1" in
  -s) printf '%s\n' "${SHIM_OS:-Linux}" ;;
  -m) printf '%s\n' "${SHIM_ARCH:-x86_64}" ;;
  *)  echo "usage: uname [-s|-m]" >&2; exit 1 ;;
esac
EOF
chmod +x "${SHIM_DIR}/uname"

cat >"${SHIM_DIR}/conan" <<EOF
#!/bin/sh
printf '%s\n' "\$*" >>"${ARGS_FILE}"
exit 0
EOF
chmod +x "${SHIM_DIR}/conan"

export PATH="${SHIM_DIR}:${PATH}"

fail() {
  echo "FAIL: $1" >&2
  exit 1
}

run_lock() {
  local host_os="$1" host_arch="$2"
  shift 2
  env "SHIM_OS=$host_os" "SHIM_ARCH=$host_arch" \
    bash "$SCRIPT_UNDER_TEST" "$@" 2>"${SHIM_DIR}/stderr"
}

# Case 1: canonical host — exit 0, full canonical settings passed to conan.
: >"$ARGS_FILE"
rc=0; run_lock Linux x86_64 >/dev/null || rc=$?
[[ $rc -eq 0 ]] || fail "canonical host: expected exit 0, got $rc"
grep -q -- '-s os=Linux' "$ARGS_FILE" || fail "canonical host: missing '-s os=Linux' in conan args"
grep -q -- '-s arch=x86_64' "$ARGS_FILE" || fail "canonical host: missing '-s arch=x86_64' in conan args"
grep -q -- '-s compiler=gcc' "$ARGS_FILE" || fail "canonical host: missing '-s compiler=gcc' in conan args"

# Case 2: mismatched host without --force — exit 1, clear message on stderr.
: >"$ARGS_FILE"
rc=0; run_lock Darwin arm64 >/dev/null || rc=$?
[[ $rc -eq 1 ]] || fail "mismatch no-force: expected exit 1, got $rc"
grep -q 'Darwin/arm64' "${SHIM_DIR}/stderr" || fail "mismatch no-force: stderr missing detected host 'Darwin/arm64'"
grep -q 'Linux/x86_64' "${SHIM_DIR}/stderr" || fail "mismatch no-force: stderr missing canonical profile 'Linux/x86_64'"
grep -q 'conan lock create' "$ARGS_FILE" && fail "mismatch no-force: conan must not run"

# Case 3: mismatched host with --force — exit 0, banner, host-portable conan args.
: >"$ARGS_FILE"
rc=0; run_lock Darwin arm64 --force >/dev/null || rc=$?
[[ $rc -eq 0 ]] || fail "mismatch force: expected exit 0, got $rc"
grep -q 'WARNING' "${SHIM_DIR}/stderr" || fail "mismatch force: stderr missing WARNING banner"
grep -q 'DO NOT commit' "${SHIM_DIR}/stderr" || fail "mismatch force: stderr missing 'DO NOT commit' notice"
grep -q -- '-s os=' "$ARGS_FILE" && fail "mismatch force: conan args must not contain '-s os='"
grep -q -- '-s compiler=gcc' "$ARGS_FILE" && fail "mismatch force: conan args must not contain '-s compiler=gcc'"
grep -q -- '-s build_type=Debug' "$ARGS_FILE" || fail "mismatch force: conan args missing portable '-s build_type=Debug'"

# Case 4: unknown argument — exit 2.
rc=0; run_lock Linux x86_64 --bogus >/dev/null || rc=$?
[[ $rc -eq 2 ]] || fail "unknown arg: expected exit 2, got $rc"

echo "PASS: lock_guard (4 cases)"
