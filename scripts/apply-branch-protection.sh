#!/usr/bin/env bash
# Idempotent: on the homeric-main-baseline ruleset, sets the pull_request rule to
#   required_approving_review_count = 1
#   dismiss_stale_reviews_on_push   = true
# Preserves all other rules (deletion, required_signatures, required_status_checks).
# Requires: gh auth login with a token holding repo scope and admin rights.
# Rollback: re-run after editing the two assignments below to the previous values
# (required_approving_review_count=1, dismiss_stale_reviews_on_push=false).
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

# Precondition: the existing pull_request rule must already carry a .parameters
# object. Refuse to write otherwise (avoids silently creating an empty rule).
PR_PARAMS_PRESENT=$(gh api "repos/${ORG}/${REPO}/rulesets/${RULESET_ID}" \
  --jq '[.rules[] | select(.type=="pull_request") | .parameters | type] | .[0] // "missing"')
if [ "${PR_PARAMS_PRESENT}" != "object" ]; then
  echo "ERROR: pull_request rule has no .parameters object (got: ${PR_PARAMS_PRESENT}) — refusing to PUT." >&2
  exit 1
fi

# Build the PUT payload: take current ruleset, patch pull_request rule in-place.
# with_entries drops top-level null values (e.g. absent bypass_actors) so they
# are omitted from the PUT rather than sent as null.
PAYLOAD=$(gh api "repos/${ORG}/${REPO}/rulesets/${RULESET_ID}" | jq '
  .rules |= map(
    if .type == "pull_request"
    then .parameters.required_approving_review_count = 1
       | .parameters.dismiss_stale_reviews_on_push   = true
    else .
    end
  )
  | {name, target, enforcement, conditions, rules, bypass_actors}
  | with_entries(select(.value != null))
')

gh api -X PUT "repos/${ORG}/${REPO}/rulesets/${RULESET_ID}" \
  --input <(echo "${PAYLOAD}") \
  --silent

# Verify: one fetch, two independent jq extractions against the captured JSON.
AFTER=$(gh api "repos/${ORG}/${REPO}/rulesets/${RULESET_ID}")

COUNT=$(echo "${AFTER}" | jq -r '
  .rules[] | select(.type=="pull_request") | .parameters.required_approving_review_count')
DISMISS=$(echo "${AFTER}" | jq -r '
  .rules[] | select(.type=="pull_request") | .parameters.dismiss_stale_reviews_on_push // "absent"')

if [ "${COUNT}" != "1" ]; then
  echo "FAIL: required_approving_review_count is '${COUNT}', expected 1" >&2
  exit 1
fi
if [ "${DISMISS}" != "true" ]; then
  echo "FAIL: dismiss_stale_reviews_on_push is '${DISMISS}', expected true (an 'absent' value means GitHub silently rejected the field)" >&2
  exit 1
fi

echo "OK: pull_request rule confirms required_approving_review_count=1, dismiss_stale_reviews_on_push=true on '${RULESET_NAME}'"
