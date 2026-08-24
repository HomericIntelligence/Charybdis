#pragma once

#include "charybdis/version.hpp"

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>

namespace charybdis {

/// Thrown when a chaos fault target subject does not match the configured
/// subject prefix. The fault was NOT injected — this is a pre-flight refusal.
class SubjectFilterViolation : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

/// Thrown when the subject-filter configuration itself is invalid (e.g. an
/// explicitly empty `CHARYBDIS_SUBJECT_PREFIX` without the explicit disable
/// flag). This is a misconfiguration, not a policy violation.
class SubjectFilterConfigError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

/// Returns true iff subject-filter enforcement is disabled via
/// `CHARYBDIS_SUBJECT_FILTER_DISABLED=1`. The check is deliberately narrow:
/// only the exact value `1` disables enforcement, to avoid ambiguous
/// truthy/falsy interpretations of arbitrary environment strings.
inline bool subject_filter_disabled() noexcept {
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const char* env = std::getenv("CHARYBDIS_SUBJECT_FILTER_DISABLED");
  return env != nullptr && std::string_view{env} == "1";
}

/// Returns the configured subject prefix: the value of
/// `CHARYBDIS_SUBJECT_PREFIX` if set (including explicitly empty), otherwise
/// the build-time default `kDefaultSubjectPrefix`.
inline std::string subject_prefix() noexcept {
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const char* env = std::getenv("CHARYBDIS_SUBJECT_PREFIX");
  if (env != nullptr) {
    return std::string{env};
  }
  return std::string{kDefaultSubjectPrefix};
}

/// Pure predicate: returns true iff `subject` starts with `prefix`.
/// An empty `prefix` matches nothing (callers must treat it as a config error).
[[nodiscard]] constexpr bool subject_allowed(std::string_view subject,
                                             std::string_view prefix) noexcept {
  return !prefix.empty() && !subject.empty() && subject.starts_with(prefix);
}

/// Fail-fast guard for chaos injection targets (issue #179).
///
/// Throws before any side effect (no HTTP call, no audit write) when:
///   * the filter is enabled and `subject` does not match the configured
///     prefix — `SubjectFilterViolation`; or
///   * the configured prefix is empty without the explicit disable flag —
///     `SubjectFilterConfigError`.
///
/// When `CHARYBDIS_SUBJECT_FILTER_DISABLED=1`, the guard returns immediately
/// and the prefix value is ignored.
inline void require_subject_allowed(std::string_view fault_type, std::string_view subject) {
  if (subject_filter_disabled()) {
    return;
  }
  const std::string prefix = subject_prefix();
  if (prefix.empty()) {
    throw SubjectFilterConfigError(
        "CHARYBDIS_SUBJECT_PREFIX is empty but CHARYBDIS_SUBJECT_FILTER_DISABLED "
        "is not set — set CHARYBDIS_SUBJECT_FILTER_DISABLED=1 to explicitly "
        "disable the subject filter");
  }
  if (!subject_allowed(subject, prefix)) {
    throw SubjectFilterViolation("Charybdis subject filter: " + std::string{fault_type} +
                                 " target must match prefix '" + prefix + "', got '" +
                                 std::string{subject} + "'");
  }
}

}  // namespace charybdis
