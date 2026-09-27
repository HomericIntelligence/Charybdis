#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <thread>

namespace charybdis {

/// Get Agamemnon URL from environment or default
inline std::string agamemnon_url() {
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const char* env = std::getenv("AGAMEMNON_URL");
  return (env != nullptr && *env != '\0') ? std::string{env}
                                          : std::string{"http://localhost:8080"};
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

/// Get the HTTP path R02 POSTs to in order to observe an injected latency fault.
///
/// R02 always issues an HTTP POST against this path with a JSON body — the env
/// var is suffixed `_POST_PATH` (not `_PATH`) to make the method requirement
/// explicit (see #95). Agamemnon's latency fault is expected to target the
/// NATS/myrmidon pipeline, not its own `/v1/health` liveness handler, so the
/// default probe is `POST /v1/teams` — the same downstream-effect endpoint
/// exercised by R04. Deployments instrumenting a different path (e.g. a
/// dedicated `/v1/chaos/latency/probe` endpoint, or `/v1/teams/{id}/tasks`)
/// can override; the override path must also accept POST.
inline std::string latency_probe_post_path() {
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const char* env = std::getenv("CHAOS_LATENCY_PROBE_POST_PATH");
  return (env != nullptr && *env != '\0') ? std::string{env} : std::string{"/v1/teams"};
}

/// Generate a random string for test isolation
inline std::string random_suffix() {
  auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

/// Monotonic per-process counter for collision-free request bodies within a
/// single test. `random_suffix()` is ms-epoch only and routinely collides on
/// back-to-back calls; concatenating this counter guarantees uniqueness within
/// the process while `random_suffix()` provides cross-run isolation.
inline std::string unique_request_tag(const std::string& prefix) {
  static std::atomic<std::uint64_t> counter{0};
  return prefix + "-" + random_suffix() + "-" + std::to_string(counter.fetch_add(1));
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

}  // namespace charybdis
