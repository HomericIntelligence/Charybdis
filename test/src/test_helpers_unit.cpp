/**
 * @file test_helpers_unit.cpp
 * @brief Unit tests for test_helpers.hpp inline utilities.
 *
 * Exercises agamemnon_url(), nats_url(), chaos_recovery_timeout(),
 * random_suffix(), and wait_until() to ensure the
 * include/projectcharybdis/test_helpers.hpp lines are covered.
 */

#include "projectcharybdis/test_helpers.hpp"

#include <chrono>
#include <cstdlib>
#include <string>

#include <gtest/gtest.h>

namespace projectcharybdis {

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

}  // namespace projectcharybdis
