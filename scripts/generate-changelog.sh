#!/usr/bin/env bash
# Generates a markdown changelog fragment from git log between two refs.
# Usage: generate-changelog.sh <FROM_TAG> <TO_REF>
set -euo pipefail

FROM_TAG="${1:-}"
TO_REF="${2:-HEAD}"

if [ -z "${FROM_TAG}" ]; then
  echo "Usage: $0 <FROM_TAG> <TO_REF>" >&2
  exit 1
fi

declare -A sections
sections=(
  [feat]="### Added"
  [fix]="### Fixed"
  [perf]="### Performance"
  [refactor]="### Changed"
  [docs]="### Documentation"
  [chore]="### Maintenance"
  [ci]="### CI"
  [test]="### Tests"
  [build]="### Build"
)

declare -A buckets

while IFS=$'\t' read -r _hash subject; do
  type="${subject%%:*}"
  type="${type%%(*}"
  type="${type// /}"
  if [[ -v "sections[${type}]" ]]; then
    body="${subject#*: }"
    buckets["${type}"]="${buckets[${type}]:-}- ${body}"$'\n'
  else
    buckets["other"]="${buckets[other]:-}- ${subject}"$'\n'
  fi
done < <(git log --pretty=format:"%H%x09%s" "${FROM_TAG}..${TO_REF}")

output=""
for type in feat fix perf refactor docs chore ci test build; do
  if [ -n "${buckets[${type}]:-}" ]; then
    output+="${sections[${type}]}"$'\n'
    output+="${buckets[${type}]}"$'\n'
  fi
done

if [ -n "${buckets[other]:-}" ]; then
  output+="### Other"$'\n'
  output+="${buckets[other]}"$'\n'
fi

if [ -z "${output}" ]; then
  output="No significant changes."$'\n'
fi

printf '%s' "${output}"
