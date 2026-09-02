#pragma once

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace charybdis {

/// Get Agamemnon URL from environment or default
inline std::string agamemnon_url() {
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const char* env = std::getenv("AGAMEMNON_URL");
  return (env != nullptr) ? std::string{env} : std::string{"http://localhost:8080"};
}

/// Get NATS URL from environment or default
inline std::string nats_url() {
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const char* env = std::getenv("NATS_URL");
  return (env != nullptr) ? std::string{env} : std::string{"nats://localhost:4222"};
}

/// Get chaos kill-fault recovery timeout (seconds) from environment, or default.
///
/// Recovery after a kill fault depends on an external supervisor (systemd unit
/// with Restart=on-failure, Kubernetes pod restart, Docker --restart=always,
/// etc.) bringing Agamemnon back up. The default 10s window is appropriate for
/// fast-restart supervisors; slower restart policies (back-off, image pull,
/// readiness probes) require a longer window. Non-positive or unparseable
/// values fall back to the default.
inline std::chrono::seconds chaos_recovery_timeout() {
  constexpr std::chrono::seconds kDefault{10};
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const char* env = std::getenv("CHAOS_RECOVERY_TIMEOUT_S");
  if (env == nullptr || *env == '\0') {
    return kDefault;
  }
  try {
    const int parsed = std::stoi(env);
    if (parsed <= 0) {
      return kDefault;
    }
    return std::chrono::seconds{parsed};
  } catch (const std::exception&) {
    return kDefault;
  }
}

/// Generate a random string for test isolation
inline std::string random_suffix() {
  auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

/// Describe a payload by length only, for safe inclusion in test failure
/// messages.
///
/// Adversarial/fuzzing payloads must never be streamed into gtest assertion
/// messages: those messages land in ctest's LastTest.log, which CI uploads as
/// an artifact on failure. This helper returns a size-only descriptor that by
/// construction contains no bytes of the payload.
inline std::string describe_payload(std::string_view payload) {
  return "payload(len=" + std::to_string(payload.size()) + ")";
}

/// Extract agent_id from an agent JSON response (handles both flat and nested shapes)
inline std::string extract_agent_id(const nlohmann::json& agent) {
  return agent.contains("id") ? agent.value("id", "")
                              : agent.value("agent", nlohmann::json{}).value("id", "");
}

/// Wait with timeout
template <typename Pred>
bool wait_until(Pred pred, std::chrono::seconds timeout = std::chrono::seconds{30}) {
  auto start = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - start < timeout) {
    if (pred()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{500});
  }
  return false;
}

/// Check whether any fault in a `GET /v1/chaos` response body carries one of
/// the given fault IDs.
///
/// Mirrors the accessors used throughout the chaos tests:
/// `body.value("faults", json::array())` + `fault.value("id", "")`. A missing
/// or non-array `faults` key is treated as "no faults listed".
///
/// @param body Parsed JSON body of a `GET /v1/chaos` response.
/// @param ids Fault IDs owned by the caller (e.g. a test's injected faults).
/// @return true if at least one listed fault's `"id"` matches any of `ids`.
inline bool faults_contain_ids(const nlohmann::json& body,
                               const std::vector<std::string>& ids) {
  const auto faults = body.value("faults", nlohmann::json::array());
  return std::ranges::any_of(faults, [&ids](const auto& fault) {
    const std::string fault_id = fault.value("id", "");
    return std::ranges::find(ids, fault_id) != ids.end();
  });
}

}  // namespace charybdis
