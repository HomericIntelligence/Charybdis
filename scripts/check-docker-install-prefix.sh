#!/usr/bin/env bash
# check-docker-install-prefix.sh — fail if the Dockerfile's builder stage
# installs into a filesystem-root prefix outside its WORKDIR, or relies on a
# root-pre-created/chowned install directory (the #248 failure class:
# `cmake --install build --prefix /install` as non-root user →
# "file cannot create directory: /install/lib").
#
# Rules (checked against the builder and runtime stages):
#   1. The builder stage declares `WORKDIR <dir>` and its
#      `cmake --install ... --prefix <P>` places `<P>` under that WORKDIR —
#      never at the filesystem root (e.g. bare `/install`).
#   2. No builder-stage RUN chowns an install prefix at the filesystem root
#      as a permission workaround (`chown ... /install`).
#   3. The runtime stage's `COPY --from=builder` binary source equals
#      `<prefix>/bin/Charybdis`, keeping both stages in lockstep.
#
# Usage: check-docker-install-prefix.sh [Dockerfile]   (default: ./Dockerfile)
# Exits 0 on success, 1 on any violation.
set -euo pipefail

usage() {
  echo "usage: $(basename "$0") [Dockerfile]" >&2
}

if [[ ${1:-} == "-h" || ${1:-} == "--help" ]]; then
  usage
  exit 0
fi

dockerfile="${1:-Dockerfile}"

if [[ ! -f "$dockerfile" ]]; then
  echo "check-docker-install-prefix: file not found: ${dockerfile}" >&2
  exit 1
fi

violations=()

report() {
  local msg="$*"
  msg="${msg//$'\n'/ }"
  echo "check-docker-install-prefix: ${dockerfile}: ${msg}" >&2
  violations+=("${msg}")
}

# Extract one named stage's body (lines after `FROM ... AS <name>` up to the
# next FROM). Case-insensitive to tolerate Dockerfile keyword casing.
extract_stage() {
  local file=$1 stage=$2
  awk -v target="$(printf '%s' "$stage" | tr '[:lower:]' '[:upper:]')" '
    toupper($1) == "FROM" {
      on = 0
      n = split($0, parts, /[[:space:]]+/)
      for (i = 1; i < n; i++) {
        if (toupper(parts[i]) == "AS") { on = (toupper(parts[i + 1]) == target) }
      }
      next
    }
    on { print }
  ' "$file"
}

builder=$(extract_stage "$dockerfile" builder)
runtime=$(extract_stage "$dockerfile" runtime)

if [[ -z "$builder" ]]; then
  report "no builder stage found (\`FROM ... AS builder\`)"
elif [[ -z "$runtime" ]]; then
  report "no runtime stage found (\`FROM ... AS runtime\`)"
else
  # Rule 1a: builder declares WORKDIR.
  workdir=$(grep -E '^[[:space:]]*WORKDIR[[:space:]]+[^[:space:]]+' <<<"$builder" \
    | tail -1 | awk '{print $2}' ||:)
  if [[ -z "$workdir" ]]; then
    report "builder stage does not declare a WORKDIR"
  fi

  # Rule 1b: cmake --install uses a prefix under that WORKDIR.
  install_line=$(grep -E 'cmake[[:space:]].*--install' <<<"$builder" | head -1 ||:)
  if [[ -z "$install_line" ]]; then
    report "builder stage has no \`cmake --install\` step"
  else
    prefix=$(sed -nE 's/.*--prefix=?[[:space:]=]*([^[:space:]]+).*/\1/p' <<<"$install_line")
    if [[ -z "$prefix" ]]; then
      report "\`cmake --install\` step declares no --prefix: ${install_line}"
    elif [[ "$prefix" != "${workdir}/"* && "$prefix" != "$workdir" ]]; then
      report "install prefix \`${prefix}\` is not under builder WORKDIR \`${workdir}\`" \
        "(root-owned absolute paths break non-root installs — issue #248)"
    fi

    # Rule 3: runtime COPY source must match <prefix>/bin/Charybdis.
    expected="${prefix}/bin/Charybdis"
    copy_src=$(grep -E -- '--from=builder' <<<"$runtime" | head -1 \
      | sed -nE 's/.*--from=builder[[:space:]]+([^[:space:]]+).*/\1/p' ||:)
    if [[ -z "$copy_src" ]]; then
      report "runtime stage has no \`COPY --from=builder ...\` step"
    elif [[ "$copy_src" != "$expected" ]]; then
      report "runtime copies \`${copy_src}\` but builder installs to \`${expected}\`" \
        "(stages out of lockstep)"
    fi
  fi

  # Rule 2: no root-level install-prefix chown workaround in the builder.
  while IFS= read -r line; do
    [[ -z "$line" ]] && continue
    report "builder-stage permission workaround for a root install prefix:" \
      "${line}"
  done < <(grep -E 'chown.*[[:space:]]/install([[:space:]]|$)' <<<"$builder" ||:)
fi

if (( ${#violations[@]} > 0 )); then
  echo >&2
  echo "check-docker-install-prefix: ${#violations[@]} violation(s) found" >&2
  echo "Install into a workspace-relative prefix inside the builder WORKDIR" >&2
  echo "(e.g. \`RUN cmake --install build --prefix /src/install\` with" >&2
  echo "\`WORKDIR /src\`) so a non-root builder never needs a root-owned path." >&2
  exit 1
fi

echo "check-docker-install-prefix: ${dockerfile} OK (install prefix inside builder WORKDIR)"
