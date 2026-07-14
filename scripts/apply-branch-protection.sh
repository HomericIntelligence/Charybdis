#!/usr/bin/env bash
# Idempotent: sets required_approving_review_count=1 on the homeric-main-baseline ruleset.
# Preserves all other rules (deletion, required_signatures, required_status_checks).
# Requires: gh auth login with a token holding repo scope and admin rights.
set -euo pipefail

ORG="HomericIntelligence"
REPO="Charybdis"
RULESET_NAME="homeric-main-baseline"

RULESET_ID=$(gh api "repos/${ORG}/${REPO}/rulesets" \
  --jq ".[] | select(.name == \"${RULESET_NAME}\") | .id")

if [ -z "${RULESET_ID}" ]; then
  echo "ERROR: ruleset '${RULESET_NAME}' not found — create it first." >&2
  exit 1
fi

echo "Found ruleset '${RULESET_NAME}' (id=${RULESET_ID})"

# Build the PUT payload: take current ruleset, patch pull_request rule in-place.
PAYLOAD=$(gh api "repos/${ORG}/${REPO}/rulesets/${RULESET_ID}" | jq '
  .rules |= map(
    if .type == "pull_request"
    then .parameters.required_approving_review_count = 1
    else .
    end
  )
  | {name, target, enforcement, conditions, rules, bypass_actors}
')

gh api -X PUT "repos/${ORG}/${REPO}/rulesets/${RULESET_ID}" \
  --input <(echo "${PAYLOAD}") \
  --silent

# Verify
COUNT=$(gh api "repos/${ORG}/${REPO}/rulesets/${RULESET_ID}" \
  --jq '.rules[] | select(.type == "pull_request") | .parameters.required_approving_review_count')

if [ "${COUNT}" != "1" ]; then
  echo "FAIL: required_approving_review_count is '${COUNT}', expected 1" >&2
  exit 1
fi

echo "OK: required_approving_review_count=1 confirmed on '${RULESET_NAME}'"
