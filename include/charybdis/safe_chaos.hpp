#pragma once

#include "charybdis/chaos_audit.hpp"
#include "charybdis/http_test_client.hpp"
#include "charybdis/subject_filter.hpp"
#include "charybdis/test_helpers.hpp"

#include <nlohmann/json.hpp>
#include <string>

namespace charybdis {

/// Inject a queue-starve fault with subject-filter enforcement (issue #179).
///
/// The subject-prefix guard runs as the first statement, so a rejected
/// subject throws `SubjectFilterViolation` BEFORE any HTTP call is attempted
/// (and therefore outside `HttpTestClient`'s retry envelope — a refusal is
/// deliberate and never retried). Rejections are recorded in the audit log
/// as `action: "filter_reject"` records before rethrowing.
///
/// NOTE: Agamemnon currently does not interpret the `subject` field in
/// `POST /v1/chaos/queue-starve`; today the enforcement value of this wrapper
/// is the client-side gate plus its audit trail. Sending the field keeps the
/// request forward-compatible with a server-side filter.
///
/// Returns the fault response body (same JSON that `ChaosResilienceTest::inject`
/// returns). The caller is responsible for tracking/cleaning up the fault id.
inline nlohmann::json safe_inject_queue_starve(HttpTestClient& client, ChaosAuditLog& audit,
                                               const std::string& subject) {
  try {
    require_subject_allowed("queue-starve", subject);
  } catch (const SubjectFilterViolation&) {
    audit.log_filter_reject("queue-starve", subject, subject_prefix());
    throw;
  }
  const auto response = client.post("/v1/chaos/queue-starve", {{"subject", subject}});
  audit.log_inject("queue-starve", agamemnon_url(), response.status, response.body);
  return response.body;
}

}  // namespace charybdis
