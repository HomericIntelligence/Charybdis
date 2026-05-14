#pragma once

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace projectcharybdis {

/// Audit trail for chaos injection events.
///
/// Issue #44: chaos test outcomes are ephemeral. For a security/resilience
/// audit trail this class emits a persistent JSON-lines record of every
/// chaos fault injection and removal, capturing:
///
///   * timestamp     — ISO-8601 UTC with millisecond precision
///   * action        — "inject" | "remove"
///   * fault_type    — e.g. "network-partition", "latency", "kill", "queue-starve"
///   * fault_id      — id returned by Agamemnon (empty for failed/unknown)
///   * target        — Agamemnon base URL the fault was sent to
///   * status        — HTTP status returned by Agamemnon (0 on transport failure)
///   * requester     — `CHAOS_AUDIT_REQUESTER` env, or `USER`, or "unknown"
///   * details       — verbatim Agamemnon response body (nlohmann::json)
///
/// Output destination is resolved on construction from the `CHAOS_AUDIT_LOG`
/// environment variable:
///   * unset / empty / "-" / "stderr"  -> std::cerr  (always available)
///   * any other value                 -> appended to that file path
///
/// If the chosen file cannot be opened, the class falls back to std::cerr
/// rather than crashing the test run, and emits a single warning line on
/// std::cerr. There is no other I/O failure path: audit emission is
/// best-effort telemetry; a chaos test must never be broken by a logging
/// problem.
///
/// **Thread-safety:** all `log_*` methods serialize writes through an
/// internal mutex, so a single `ChaosAuditLog` instance is safe to share
/// across threads in the test process.
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions,hicpp-special-member-functions)
class ChaosAuditLog {
 public:
  ChaosAuditLog() {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char* dest = std::getenv("CHAOS_AUDIT_LOG");
    if (dest != nullptr && *dest != '\0' && std::string_view{dest} != "-" &&
        std::string_view{dest} != "stderr") {
      file_.open(dest, std::ios::app);
      if (!file_.is_open()) {
        std::cerr << "{\"chaos_audit_warning\":\"failed to open CHAOS_AUDIT_LOG="
                  << dest << "; falling back to stderr\"}\n";
      } else {
        path_ = dest;
      }
    }
  }

  ~ChaosAuditLog() {
    if (file_.is_open()) {
      file_.flush();
      file_.close();
    }
  }

  /// Log a fault injection attempt.
  void log_inject(std::string_view fault_type, std::string_view target, int http_status,
                  const nlohmann::json& response_body) {
    emit("inject", fault_type, target, http_status, response_body);
  }

  /// Log a fault removal attempt. `fault_id` is the id being removed (may be
  /// non-empty even when removal failed).
  void log_remove(std::string_view fault_type, std::string_view fault_id,
                  std::string_view target, int http_status,
                  const nlohmann::json& response_body) {
    emit_remove(fault_type, fault_id, target, http_status, response_body);
  }

  /// Test/diagnostic accessor: file path actually being written, or empty
  /// string if events go to stderr.
  [[nodiscard]] const std::string& path() const { return path_; }

 private:
  static std::string iso8601_utc_now() {
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const auto secs = std::chrono::time_point_cast<std::chrono::seconds>(now);
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - secs).count();
    const std::time_t tt = clock::to_time_t(secs);
    std::tm tm_buf{};
    // NOLINTNEXTLINE(concurrency-mt-unsafe) — gmtime_r is the thread-safe form
    gmtime_r(&tt, &tm_buf);
    char buf[32] = {};
    // YYYY-MM-DDTHH:MM:SS
    std::strftime(static_cast<char*>(buf), sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
    char out[40] = {};
    std::snprintf(static_cast<char*>(out), sizeof(out), "%s.%03lldZ",
                  static_cast<const char*>(buf), static_cast<long long>(millis));
    return std::string{static_cast<const char*>(out)};
  }

  static std::string current_requester() {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char* req = std::getenv("CHAOS_AUDIT_REQUESTER");
    if (req != nullptr && *req != '\0') {
      return std::string{req};
    }
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char* user = std::getenv("USER");
    if (user != nullptr && *user != '\0') {
      return std::string{user};
    }
    return std::string{"unknown"};
  }

  void emit(std::string_view action, std::string_view fault_type, std::string_view target,
            int http_status, const nlohmann::json& response_body) {
    nlohmann::json record = {
        {"schema_version", 1},
        {"timestamp", iso8601_utc_now()},
        {"action", std::string{action}},
        {"fault_type", std::string{fault_type}},
        {"fault_id", response_body.value("id", "")},
        {"target", std::string{target}},
        {"status", http_status},
        {"requester", current_requester()},
        {"details", response_body},
    };
    write_line(record);
  }

  void emit_remove(std::string_view fault_type, std::string_view fault_id,
                   std::string_view target, int http_status,
                   const nlohmann::json& response_body) {
    nlohmann::json record = {
        {"schema_version", 1},
        {"timestamp", iso8601_utc_now()},
        {"action", "remove"},
        {"fault_type", std::string{fault_type}},
        {"fault_id", std::string{fault_id}},
        {"target", std::string{target}},
        {"status", http_status},
        {"requester", current_requester()},
        {"details", response_body},
    };
    write_line(record);
  }

  void write_line(const nlohmann::json& record) {
    const std::lock_guard<std::mutex> guard(mu_);
    std::ostream& out = file_.is_open() ? static_cast<std::ostream&>(file_) : std::cerr;
    out << record.dump() << '\n';
    out.flush();
  }

  std::mutex mu_;
  std::ofstream file_;
  std::string path_;
};

}  // namespace projectcharybdis
