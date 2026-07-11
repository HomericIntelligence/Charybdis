/**
 * @file test_chaos_audit_unit.cpp
 * @brief Unit tests for ChaosAuditLog (issue #44).
 *
 * Verifies:
 *   * File-mode emission writes JSON-lines with the expected envelope.
 *   * Each record carries timestamp, action, fault_type, fault_id, target,
 *     status, requester, schema_version, details.
 *   * Inject and remove actions are distinguishable.
 *   * Unset / unwritable CHAOS_AUDIT_LOG falls back to stderr without
 *     crashing the test process.
 *
 * No external services required; runs in the unit-test target.
 */

#include "charybdis/chaos_audit.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

namespace charybdis {
namespace {

class ChaosAuditLogTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto tmp =
        std::filesystem::temp_directory_path() /
        ("charybdis-audit-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
         "-" + ::testing::UnitTest::GetInstance()->current_test_info()->name() + ".jsonl");
    path_ = tmp.string();
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    ::setenv("CHAOS_AUDIT_LOG", path_.c_str(), 1);
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    ::setenv("CHAOS_AUDIT_REQUESTER", "unit-test", 1);
  }

  void TearDown() override {
    std::error_code err_code;
    std::filesystem::remove(path_, err_code);
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    ::unsetenv("CHAOS_AUDIT_LOG");
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    ::unsetenv("CHAOS_AUDIT_REQUESTER");
  }

  static std::string slurp(const std::string& path) {
    const std::ifstream input{path};
    std::stringstream stream;
    stream << input.rdbuf();
    return stream.str();
  }

  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
  std::string path_;
};

TEST_F(ChaosAuditLogTest, InjectWritesEnvelopedJsonLine) {
  const nlohmann::json body = {{"id", "f-123"}, {"type", "latency"}, {"active", true}};
  {
    ChaosAuditLog audit;
    EXPECT_EQ(audit.path(), path_);
    audit.log_inject("latency", "http://agamemnon:8080", 201, body);
  }

  auto contents = slurp(path_);
  ASSERT_FALSE(contents.empty());
  auto record = nlohmann::json::parse(contents);

  EXPECT_EQ(record.value("schema_version", 0), 1);
  EXPECT_EQ(record.value("action", ""), "inject");
  EXPECT_EQ(record.value("fault_type", ""), "latency");
  EXPECT_EQ(record.value("fault_id", ""), "f-123");
  EXPECT_EQ(record.value("target", ""), "http://agamemnon:8080");
  EXPECT_EQ(record.value("status", 0), 201);
  EXPECT_EQ(record.value("requester", ""), "unit-test");
  ASSERT_TRUE(record.contains("timestamp"));
  EXPECT_FALSE(record.value("timestamp", "").empty());
  ASSERT_TRUE(record.contains("details"));
  EXPECT_EQ(record["details"], body);
}

TEST_F(ChaosAuditLogTest, RemoveActionIsDistinguishable) {
  {
    ChaosAuditLog audit;
    audit.log_remove("kill", "f-9", "http://a:1", 204, nlohmann::json::object());
  }
  auto record = nlohmann::json::parse(slurp(path_));
  EXPECT_EQ(record.value("action", ""), "remove");
  EXPECT_EQ(record.value("fault_id", ""), "f-9");
}

TEST_F(ChaosAuditLogTest, MultipleEventsAreNewlineDelimited) {
  {
    ChaosAuditLog audit;
    audit.log_inject("kill", "http://a", 201, nlohmann::json{{"id", "f-1"}});
    audit.log_inject("latency", "http://a", 201, nlohmann::json{{"id", "f-2"}});
    audit.log_remove("kill", "f-1", "http://a", 204, nlohmann::json::object());
  }
  auto contents = slurp(path_);
  std::size_t lines = 0;
  for (const char chr : contents) {
    if (chr == '\n') {
      ++lines;
    }
  }
  EXPECT_EQ(lines, 3U);
}

TEST(ChaosAuditLogStderr, FallsBackWhenEnvUnset) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  ::unsetenv("CHAOS_AUDIT_LOG");
  ChaosAuditLog audit;
  EXPECT_TRUE(audit.path().empty());
  // Must not crash: writes to stderr.
  audit.log_inject("kill", "http://a", 201, nlohmann::json{{"id", "f-x"}});
}

TEST(ChaosAuditLogStderr, FallsBackWhenEnvIsDash) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  ::setenv("CHAOS_AUDIT_LOG", "-", 1);
  ChaosAuditLog audit;
  EXPECT_TRUE(audit.path().empty());
  audit.log_inject("kill", "http://a", 201, nlohmann::json{{"id", "f-y"}});
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  ::unsetenv("CHAOS_AUDIT_LOG");
}

TEST(ChaosAuditLogStderr, RequesterDefaultsWhenEnvUnset) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  ::unsetenv("CHAOS_AUDIT_REQUESTER");
  // We cannot easily capture stderr here, but the call must not throw.
  ChaosAuditLog audit;
  audit.log_inject("kill", "http://a", 201, nlohmann::json{{"id", "f-z"}});
}

}  // namespace
}  // namespace charybdis
