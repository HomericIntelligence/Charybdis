#pragma once

#include <chrono>
#include <cstdlib>
#include <exception>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

namespace projectcharybdis {

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

}  // namespace projectcharybdis
