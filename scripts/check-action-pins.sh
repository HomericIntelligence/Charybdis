#!/usr/bin/env bash
# check-action-pins.sh — fail if any GitHub Action reference is not pinned
# to an immutable 40-char SHA with a trailing `# v<tag>` comment.
#
# Rules:
#   * `uses: owner/repo@<40-hex-sha>  # v<anything>`              OK
#   * `uses: ./.github/actions/<local>` (local composite action)  OK
#   * `uses: docker://image@sha256:<digest>` (docker reference)   OK
#   * anything else                                               FAIL
#
# Exits 0 on success, 1 on any violation.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Files to check: workflows + composite actions
mapfile -t files < <(
  find "${ROOT_DIR}/.github/workflows" "${ROOT_DIR}/.github/actions" \
       -type f \( -name '*.yml' -o -name '*.yaml' \) 2>/dev/null | sort
)

if [[ ${#files[@]} -eq 0 ]]; then
  echo "check-action-pins: no workflow or composite-action files found"
  exit 0
fi

violations=0
checked=0
report=()

for f in "${files[@]}"; do
  # Match every `uses:` line (any indent, value may be quoted)
  while IFS=: read -r lineno content; do
    # Strip leading whitespace and the leading `uses:` token
    value="${content#*uses:}"
    # Trim leading/trailing whitespace
    value="${value#"${value%%[![:space:]]*}"}"
    value="${value%"${value##*[![:space:]]}"}"
    # Drop surrounding quotes if present
    value="${value%\"}"; value="${value#\"}"
    value="${value%\'}"; value="${value#\'}"

    # Skip blanks (shouldn't happen — grep ensures content)
    [[ -z "$value" ]] && continue

    checked=$((checked + 1))

    # Allow local composite actions
    if [[ "$value" =~ ^\./ ]]; then continue; fi
    # Allow docker:// SHA-256 digests
    if [[ "$value" =~ ^docker://[^@]+@sha256:[0-9a-f]{64}$ ]]; then continue; fi
    # Require <owner>/<repo>(/<path>)?@<40-hex>  optional `  # v<...>` trailer
    if [[ "$value" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_./-]+@[0-9a-f]{40}([[:space:]]+#[[:space:]]*v.+)?$ ]]; then
      continue
    fi

    violations=$((violations + 1))
    rel="${f#"${ROOT_DIR}"/}"
    report+=("${rel}:${lineno}: ${value}")
  done < <(grep -nE '^[[:space:]]*(-[[:space:]]+)?uses:[[:space:]]*[^[:space:]]' "$f" || true)
done

if (( violations > 0 )); then
  echo "check-action-pins: ${violations} un-SHA-pinned action reference(s) found" >&2
  for line in "${report[@]}"; do
    echo "  ${line}" >&2
  done
  echo >&2
  echo "Each \`uses:\` must reference a 40-char commit SHA with a trailing" >&2
  echo "  \`# v<tag>\` comment, e.g." >&2
  echo "    uses: actions/checkout@de0fac2e4500dabe0009e67214ff5f5447ce83dd  # v6.0.2" >&2
  echo "Resolve tags to SHAs with \`gh api repos/<owner>/<repo>/commits/<tag>\`," >&2
  echo "or with a tool such as \`pin-github-action\` or \`ratchet\`." >&2
  exit 1
fi

echo "check-action-pins: ${checked} \`uses:\` reference(s) checked, all SHA-pinned"
