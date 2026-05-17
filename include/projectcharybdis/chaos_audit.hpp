#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <sstream>
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
    if (dest == nullptr || *dest == '\0' || std::string_view{dest} == "-" ||
        std::string_view{dest} == "stderr") {
      return;
    }
    const std::string_view dest_view{dest};
    // Reject path-traversal and other untrusted-input footguns before
    // touching the filesystem. CHAOS_AUDIT_LOG flows in from the
    // environment, so we treat it as tainted: only accept paths free of
    // embedded NULs and `..` traversal segments.
    if (dest_view.find('\0') != std::string_view::npos) {
      std::cerr
          << R"({"chaos_audit_warning":"CHAOS_AUDIT_LOG contains embedded NUL; falling back to stderr"})"
          << '\n';
      return;
    }
    const std::filesystem::path candidate(dest_view);
    for (const auto& part : candidate) {
      if (part == "..") {
        std::cerr
            << R"({"chaos_audit_warning":"CHAOS_AUDIT_LOG rejects parent-directory traversal; falling back to stderr"})"
            << '\n';
        return;
      }
    }
    std::error_code err_code;
    const std::filesystem::path resolved = std::filesystem::weakly_canonical(candidate, err_code);
    const std::filesystem::path& open_path = err_code ? candidate : resolved;
    file_.open(open_path, std::ios::app);
    if (!file_.is_open()) {
      std::cerr << R"({"chaos_audit_warning":"failed to open CHAOS_AUDIT_LOG=)"
                << open_path.string() << R"(; falling back to stderr"})" << '\n';
    } else {
      // Preserve the caller-supplied destination string for the public
      // accessor: callers reason about the path they passed via the
      // environment, not the canonicalised form used internally to open
      // the file.
      path_ = std::string{dest_view};
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
  void log_remove(std::string_view fault_type, std::string_view fault_id, std::string_view target,
                  int http_status, const nlohmann::json& response_body) {
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
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now - secs).count();
    const std::time_t time_t_val = clock::to_time_t(secs);
    std::tm tm_buf{};
    // gmtime_r is the thread-safe form; available on POSIX targets we build for.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    gmtime_r(&time_t_val, &tm_buf);

    // YYYY-MM-DDTHH:MM:SS is 19 chars; 24 leaves headroom for the NUL.
    std::array<char, 24> date_buf{};
    const std::size_t date_written =
        std::strftime(date_buf.data(), date_buf.size(), "%Y-%m-%dT%H:%M:%S", &tm_buf);
    if (date_written == 0) {
      // strftime cannot encode this calendar value into the buffer; fall back
      // to a well-formed sentinel so audit consumers can still parse the line.
      return std::string{"1970-01-01T00:00:00.000Z"};
    }

    // Compose the trailing ".NNNZ" with std::ostringstream to avoid C-vararg
    // snprintf (cppcoreguidelines-pro-type-vararg) while keeping the
    // strftime-formatted date portion verbatim.
    std::ostringstream out;
    out << date_buf.data() << '.' << std::setw(3) << std::setfill('0')
        << static_cast<std::int64_t>(millis) << 'Z';
    return out.str();
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
    const nlohmann::json record = {
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

  void emit_remove(std::string_view fault_type, std::string_view fault_id, std::string_view target,
                   int http_status, const nlohmann::json& response_body) {
    const nlohmann::json record = {
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
