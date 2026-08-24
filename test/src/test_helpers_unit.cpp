/**
 * @file test_helpers_unit.cpp
 * @brief Unit tests for test_helpers.hpp inline utilities.
 *
 * Exercises agamemnon_url(), nats_url(), chaos_recovery_timeout(),
 * random_suffix(), wait_until(), and faults_contain_ids() to ensure the
 * include/charybdis/test_helpers.hpp lines are covered.
 */

#include "charybdis/test_helpers.hpp"

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace charybdis {

TEST(TestHelpersUnit, AgamemnonUrlDefaultsToLocalhost) {
  // Without AGAMEMNON_URL set, should return the default
  const std::string url = agamemnon_url();
  EXPECT_FALSE(url.empty());
  EXPECT_NE(url.find("localhost"), std::string::npos);
}

TEST(TestHelpersUnit, NatsUrlDefaultsToLocalhost) {
  // Without NATS_URL set, should return the default
  const std::string url = nats_url();
  EXPECT_FALSE(url.empty());
  EXPECT_NE(url.find("localhost"), std::string::npos);
}

TEST(TestHelpersUnit, ChaosRecoveryTimeoutDefaultsTo10s) {
  // Unset the var so the default branch is exercised.
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  ::unsetenv("CHAOS_RECOVERY_TIMEOUT_S");
  EXPECT_EQ(chaos_recovery_timeout(), std::chrono::seconds{10});
}

TEST(TestHelpersUnit, ChaosRecoveryTimeoutHonorsValidEnv) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  ::setenv("CHAOS_RECOVERY_TIMEOUT_S", "45", 1);
  EXPECT_EQ(chaos_recovery_timeout(), std::chrono::seconds{45});
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  ::unsetenv("CHAOS_RECOVERY_TIMEOUT_S");
}

TEST(TestHelpersUnit, ChaosRecoveryTimeoutFallsBackOnGarbage) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  ::setenv("CHAOS_RECOVERY_TIMEOUT_S", "not-a-number", 1);
  EXPECT_EQ(chaos_recovery_timeout(), std::chrono::seconds{10});
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  ::setenv("CHAOS_RECOVERY_TIMEOUT_S", "0", 1);
  EXPECT_EQ(chaos_recovery_timeout(), std::chrono::seconds{10});
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  ::setenv("CHAOS_RECOVERY_TIMEOUT_S", "-5", 1);
  EXPECT_EQ(chaos_recovery_timeout(), std::chrono::seconds{10});
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  ::setenv("CHAOS_RECOVERY_TIMEOUT_S", "", 1);
  EXPECT_EQ(chaos_recovery_timeout(), std::chrono::seconds{10});
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  ::unsetenv("CHAOS_RECOVERY_TIMEOUT_S");
}

TEST(TestHelpersUnit, RandomSuffixIsNonEmpty) {
  const std::string suffix = random_suffix();
  EXPECT_FALSE(suffix.empty());
}

TEST(TestHelpersUnit, RandomSuffixIsNumeric) {
  // random_suffix() returns a decimal string of the current epoch in ms
  const std::string suffix = random_suffix();
  EXPECT_FALSE(suffix.empty());
  // All characters should be decimal digits
  for (const char chr : suffix) {
    EXPECT_TRUE(chr >= '0' && chr <= '9') << "Non-digit in suffix: " << chr;
  }
}

TEST(TestHelpersUnit, DescribePayloadReportsLengthOnly) {
  const std::string payload = "secret-content";
  EXPECT_EQ(describe_payload(payload), "payload(len=14)");
  EXPECT_EQ(describe_payload(std::string_view{}), "payload(len=0)");
}

TEST(TestHelpersUnit, DescribePayloadNeverContainsInputBytesPrintable) {
  const std::string_view payload = "sensitive printable text 12345";
  const std::string description = describe_payload(payload);
  // No substring of the input (longer than a single char) may appear
  EXPECT_EQ(description.find("sensitive"), std::string::npos);
  for (std::size_t len = 2; len <= payload.size(); ++len) {
    EXPECT_EQ(description.find(payload.substr(0, len)), std::string::npos)
        << "Leak at prefix length " << len;
  }
}

TEST(TestHelpersUnit, DescribePayloadNeverContainsInputBytesBinary) {
  // All 256 byte values, including NUL and non-ASCII bytes
  std::string payload;
  payload.reserve(256);
  for (int value = 0; value < 256; ++value) {
    payload.push_back(static_cast<char>(value));
  }
  const std::string description = describe_payload(payload);
  EXPECT_EQ(description, "payload(len=256)");
  // The descriptor is pure printable ASCII; no raw payload byte survives.
  for (const char chr : description) {
    const auto byte = static_cast<unsigned char>(chr);
    EXPECT_GE(byte, 0x20U);
    EXPECT_LT(byte, 0x7FU);
  }
}

TEST(TestHelpersUnit, DescribePayloadOversizedInput) {
  // NOLINTNEXTLINE(bugprone-implicit-widening-of-multiplication-result,misc-const-correctness)
  const std::string payload(5 * 1024 * 1024, 'A');
  const std::string description = describe_payload(payload);
  EXPECT_EQ(description, "payload(len=" + std::to_string(payload.size()) + ")");
  // Output stays bounded regardless of input size
  EXPECT_LE(description.size(), static_cast<std::size_t>(64));
  EXPECT_EQ(description.find('A'), std::string::npos);
}

TEST(TestHelpersUnit, WaitUntilReturnsTrueImmediately) {
  // Predicate that is already true — should return true with no waiting
  const bool result = wait_until([]() { return true; }, std::chrono::seconds{1});
  EXPECT_TRUE(result);
}

TEST(TestHelpersUnit, WaitUntilReturnsFalseOnTimeout) {
  // Predicate that is never true — should time out and return false
  const bool result = wait_until([]() { return false; }, std::chrono::seconds{1});
  EXPECT_FALSE(result);
}

TEST(TestHelpersUnit, WaitUntilReturnsTrueAfterDelay) {
  // Predicate that becomes true after a few polls
  int count = 0;
  const bool result = wait_until(
      [&count]() {
        ++count;
        return count >= 3;
      },
      std::chrono::seconds{5});
  EXPECT_TRUE(result);
  EXPECT_GE(count, 3);
}

TEST(TestHelpersUnit, FaultsContainIdsEmptyFaultListReturnsFalse) {
  const nlohmann::json body = {{"faults", nlohmann::json::array()}};
  EXPECT_FALSE(faults_contain_ids(body, {"fault-a"}));
}

TEST(TestHelpersUnit, FaultsContainIdsMatchingIdReturnsTrue) {
  const nlohmann::json body = {
      {"faults", nlohmann::json::array({{{"id", "fault-a"}, {"type", "latency"}}})}};
  EXPECT_TRUE(faults_contain_ids(body, {"fault-a"}));
}

TEST(TestHelpersUnit, FaultsContainIdsNonMatchingIdsOnlyReturnsFalse) {
  const nlohmann::json body = {
      {"faults",
       nlohmann::json::array({{{"id", "other-1"}}, {{"id", "other-2"}}})}};
  EXPECT_FALSE(faults_contain_ids(body, {"fault-a", "fault-b"}));
}

TEST(TestHelpersUnit, FaultsContainIdsMissingFaultsKeyReturnsFalse) {
  const nlohmann::json body = nlohmann::json::object();
  EXPECT_FALSE(faults_contain_ids(body, {"fault-a"}));
}

TEST(TestHelpersUnit, FaultsContainIdsMultipleTrackedWithOneLingeringReturnsTrue) {
  const nlohmann::json body = {
      {"faults",
       nlohmann::json::array({{{"id", "unrelated"}}, {{"id", "fault-b"}}})}};
  EXPECT_TRUE(faults_contain_ids(body, {"fault-a", "fault-b"}));
}

}  // namespace charybdis
