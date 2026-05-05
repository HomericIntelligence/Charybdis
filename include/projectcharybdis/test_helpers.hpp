#pragma once

#include <chrono>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

namespace projectcharybdis {

/// Get Agamemnon URL from environment or default
inline std::string agamemnon_url() {
  const char* env = std::getenv("AGAMEMNON_URL");
  return env ? env : "http://localhost:8080";
}

/// Get NATS URL from environment or default
inline std::string nats_url() {
  const char* env = std::getenv("NATS_URL");
  return env ? env : "nats://localhost:4222";
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
    if (pred()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds{500});
  }
  return false;
}

}  // namespace projectcharybdis
