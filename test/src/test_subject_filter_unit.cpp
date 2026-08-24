/**
 * @file test_subject_filter_unit.cpp
 * @brief Unit tests for the subject-filter enforcement guard (issue #179).
 *
 * No external services required; runs in the unit-test target. Each case
 * isolates environment state via a ScopedEnv RAII helper so env-var leakage
 * between tests cannot cause ordering-dependent results.
 */

#include "charybdis/http_test_client.hpp"
#include "charybdis/subject_filter.hpp"

#include <cstdlib>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace charybdis {
namespace {

/// RAII helper: sets (or unsets) an environment variable for the scope and
/// restores the previous value on destruction.
class ScopedEnv {
 public:
  ScopedEnv(std::string_view name, const std::string* value) : name_{name} {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char* prev = std::getenv(name_.c_str());
    had_prev_ = prev != nullptr;
    if (had_prev_) {
      prev_ = std::string{prev};
    }
    apply(value);
  }

  ScopedEnv(const ScopedEnv&) = delete;
  ScopedEnv& operator=(const ScopedEnv&) = delete;
  ScopedEnv(ScopedEnv&&) = delete;
  ScopedEnv& operator=(ScopedEnv&&) = delete;

  ~ScopedEnv() {
    if (had_prev_) {
      // NOLINTNEXTLINE(concurrency-mt-unsafe)
      ::setenv(name_.c_str(), prev_.c_str(), 1);
    } else {
      // NOLINTNEXTLINE(concurrency-mt-unsafe)
      ::unsetenv(name_.c_str());
    }
  }

  void set(const std::string& value) { apply(&value); }

  void unset() { apply(nullptr); }

 private:
  void apply(const std::string* value) {
    if (value == nullptr) {
      // NOLINTNEXTLINE(concurrency-mt-unsafe)
      ::unsetenv(name_.c_str());
    } else {
      // NOLINTNEXTLINE(concurrency-mt-unsafe)
      ::setenv(name_.c_str(), value->c_str(), 1);
    }
  }

  std::string name_;
  bool had_prev_{false};
  std::string prev_;
};

TEST(SubjectFilterTest, DefaultPrefixIsHiTest) {
  static_assert(kDefaultSubjectPrefix == "hi.test.",
                "the documented default subject prefix must be hi.test.");
  static_assert(subject_allowed("hi.test.foo", kDefaultSubjectPrefix));
  SUCCEED();
}

TEST(SubjectFilterTest, MatchingSubjectPasses) {
  ScopedEnv prefix{"CHARYBDIS_SUBJECT_PREFIX", nullptr};
  ScopedEnv disabled{"CHARYBDIS_SUBJECT_FILTER_DISABLED", nullptr};
  EXPECT_NO_THROW(require_subject_allowed("queue-starve", "hi.test.foo"));
  EXPECT_NO_THROW(require_subject_allowed("queue-starve", "hi.test."));
}

TEST(SubjectFilterTest, NonMatchingSubjectThrowsViolation) {
  ScopedEnv prefix{"CHARYBDIS_SUBJECT_PREFIX", nullptr};
  ScopedEnv disabled{"CHARYBDIS_SUBJECT_FILTER_DISABLED", nullptr};
  try {
    require_subject_allowed("queue-starve", "prod.orders.>");
    FAIL() << "expected SubjectFilterViolation";
  } catch (const SubjectFilterViolation& err) {
    const std::string message = err.what();
    EXPECT_NE(message.find("hi.test."), std::string::npos)
        << "message must name the configured prefix: " << message;
    EXPECT_NE(message.find("prod.orders.>"), std::string::npos)
        << "message must name the rejected subject: " << message;
  }
}

TEST(SubjectFilterTest, EnvVarOverridesDefault) {
  ScopedEnv prefix{"CHARYBDIS_SUBJECT_PREFIX", nullptr};
  ScopedEnv disabled{"CHARYBDIS_SUBJECT_FILTER_DISABLED", nullptr};
  prefix.set("stage.");
  EXPECT_NO_THROW(require_subject_allowed("queue-starve", "stage.x"));
  EXPECT_THROW(require_subject_allowed("queue-starve", "hi.test.x"), SubjectFilterViolation);
}

TEST(SubjectFilterTest, EmptyPrefixWithoutDisableThrowsConfigError) {
  ScopedEnv prefix{"CHARYBDIS_SUBJECT_PREFIX", nullptr};
  ScopedEnv disabled{"CHARYBDIS_SUBJECT_FILTER_DISABLED", nullptr};
  prefix.set("");
  try {
    require_subject_allowed("queue-starve", "hi.test.foo");
    FAIL() << "expected SubjectFilterConfigError";
  } catch (const SubjectFilterConfigError& err) {
    const std::string message = err.what();
    EXPECT_NE(message.find("CHARYBDIS_SUBJECT_FILTER_DISABLED"), std::string::npos)
        << "message must point at the disable flag: " << message;
  }
}

TEST(SubjectFilterTest, DisableFlagAloneAllowsAnySubject) {
  ScopedEnv prefix{"CHARYBDIS_SUBJECT_PREFIX", nullptr};
  ScopedEnv disabled{"CHARYBDIS_SUBJECT_FILTER_DISABLED", nullptr};
  disabled.set("1");
  // Prefix left at its default (hi.test.): the disable flag wins outright.
  EXPECT_NO_THROW(require_subject_allowed("queue-starve", "prod.orders.>"));
}

TEST(SubjectFilterTest, DisableFlagWithExplicitEmptyPrefixAllowsAnySubject) {
  ScopedEnv prefix{"CHARYBDIS_SUBJECT_PREFIX", nullptr};
  ScopedEnv disabled{"CHARYBDIS_SUBJECT_FILTER_DISABLED", nullptr};
  prefix.set("");
  disabled.set("1");
  EXPECT_NO_THROW(require_subject_allowed("queue-starve", "prod.orders.>"));
}

TEST(SubjectFilterTest, EmptySubjectUnderActivePrefixThrowsViolation) {
  ScopedEnv prefix{"CHARYBDIS_SUBJECT_PREFIX", nullptr};
  ScopedEnv disabled{"CHARYBDIS_SUBJECT_FILTER_DISABLED", nullptr};
  EXPECT_THROW(require_subject_allowed("queue-starve", ""), SubjectFilterViolation);
}

// ── Choke-point enforcement inside HttpTestClient (issue #179 review) ────────
//
// The gate must hold for ANY caller of HttpTestClient::post, not only the
// safe_inject_queue_starve wrapper. Port 1 is used so a connection-refused
// failure (status == 0) happens immediately without a live server.

TEST(HttpClientSubjectGateTest, PostQueueStarveRejectsNonMatchingSubject) {
  ScopedEnv prefix{"CHARYBDIS_SUBJECT_PREFIX", nullptr};
  ScopedEnv disabled{"CHARYBDIS_SUBJECT_FILTER_DISABLED", nullptr};
  HttpTestClient client("http://127.0.0.1:1");
  HttpTestClient::Response response{0, {}};
  EXPECT_THROW(response = client.post("/v1/chaos/queue-starve", {{"subject", "prod.orders.>"}}),
               SubjectFilterViolation);
}

TEST(HttpClientSubjectGateTest, PostQueueStarveRejectsMissingSubject) {
  ScopedEnv prefix{"CHARYBDIS_SUBJECT_PREFIX", nullptr};
  ScopedEnv disabled{"CHARYBDIS_SUBJECT_FILTER_DISABLED", nullptr};
  HttpTestClient client("http://127.0.0.1:1");
  HttpTestClient::Response response{0, {}};
  EXPECT_THROW(response = client.post("/v1/chaos/queue-starve"), SubjectFilterViolation);
}

TEST(HttpClientSubjectGateTest, PostQueueStarveRejectsEmptySubjectField) {
  ScopedEnv prefix{"CHARYBDIS_SUBJECT_PREFIX", nullptr};
  ScopedEnv disabled{"CHARYBDIS_SUBJECT_FILTER_DISABLED", nullptr};
  HttpTestClient client("http://127.0.0.1:1");
  HttpTestClient::Response response{0, {}};
  EXPECT_THROW(response = client.post("/v1/chaos/queue-starve", {{"subject", ""}}),
               SubjectFilterViolation);
}

TEST(HttpClientSubjectGateTest, PostQueueStarveAllowsNamespacedSubjectThroughGate) {
  ScopedEnv prefix{"CHARYBDIS_SUBJECT_PREFIX", nullptr};
  ScopedEnv disabled{"CHARYBDIS_SUBJECT_FILTER_DISABLED", nullptr};
  HttpTestClient client("http://127.0.0.1:1");
  HttpTestClient::Response response{0, {}};
  EXPECT_NO_THROW(response = client.post("/v1/chaos/queue-starve", {{"subject", "hi.test.gated"}}));
  EXPECT_EQ(response.status, 0);  // connection refused: the guard did not fire
}

TEST(HttpClientSubjectGateTest, DisableFlagAllowsNonMatchingSubjectThroughHttpPostGate) {
  ScopedEnv prefix{"CHARYBDIS_SUBJECT_PREFIX", nullptr};
  ScopedEnv disabled{"CHARYBDIS_SUBJECT_FILTER_DISABLED", nullptr};
  disabled.set("1");
  HttpTestClient client("http://127.0.0.1:1");
  HttpTestClient::Response response{0, {}};
  EXPECT_NO_THROW(response = client.post("/v1/chaos/queue-starve", {{"subject", "prod.orders.>"}}));
  EXPECT_EQ(response.status, 0);  // connection refused: the gate was bypassed
}

TEST(HttpClientSubjectGateTest, OtherEndpointsAreNotGated) {
  ScopedEnv prefix{"CHARYBDIS_SUBJECT_PREFIX", nullptr};
  ScopedEnv disabled{"CHARYBDIS_SUBJECT_FILTER_DISABLED", nullptr};
  HttpTestClient client("http://127.0.0.1:1");
  HttpTestClient::Response response{0, {}};
  EXPECT_NO_THROW(response = client.post("/v1/chaos/kill", {{"service", "prod"}}));
  EXPECT_EQ(response.status, 0);
}

}  // namespace
}  // namespace charybdis
