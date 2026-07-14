/**
 * @file test_payload_fuzzing.cpp
 * @brief E05-E07, E14, E15: Adversarial payload fuzzing via Agamemnon REST API
 *
 * Sends malformed, oversized, truncated, and structurally invalid payloads.
 * Verifies the server handles them gracefully (no crash, returns error codes).
 *
 * Requires: Agamemnon running at AGAMEMNON_URL
 */

#include "charybdis/http_test_client.hpp"
#include "charybdis/test_helpers.hpp"

#include <memory>
#include <nlohmann/json.hpp>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace charybdis {

// NOLINTNEXTLINE(misc-use-internal-linkage)
class PayloadFuzzingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    client_ = std::make_unique<HttpTestClient>(agamemnon_url());
    // NOLINTNEXTLINE(readability-implicit-bool-conversion)
    if (!client_->is_healthy()) {
      GTEST_SKIP() << "Agamemnon not reachable at " << agamemnon_url();
    }
  }

  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
  std::unique_ptr<HttpTestClient> client_;
};

// E05: Random byte strings as payloads
TEST_F(PayloadFuzzingTest, E05RandomByteStrings) {
  // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp,misc-const-correctness)
  std::mt19937 gen(42);  // Deterministic seed for reproducibility; mutated by dist(gen)
  std::uniform_int_distribution<> dist(0, 255);

  for (int i = 0; i < 50; ++i) {
    // NOLINTNEXTLINE(bugprone-unused-local-non-trivial-variable)
    std::string payload(100, '\0');
    for (auto& chr : payload) {
      chr = static_cast<char>(dist(gen));
    }

    auto [status, body] = client_->post_raw("/v1/agents", payload);
    // Any non-5xx response is acceptable (400, 422, etc.)
    // NOLINTNEXTLINE(readability-implicit-bool-conversion)
    EXPECT_NE(status, 500) << "Server returned 500 on random payload #" << i;
  }

  // Verify server survived
  // NOLINTNEXTLINE(readability-implicit-bool-conversion)
  EXPECT_TRUE(client_->is_healthy()) << "Server crashed after 50 random payloads";
}

// E06: Truncated JSON
TEST_F(PayloadFuzzingTest, E06TruncatedJson) {
  const std::vector<std::string> truncated = {
      R"({"name": "test)",        // Cut mid-string
      R"({"name": "test", "la)",  // Cut mid-key
      R"({)",                     // Just opening brace
      R"({"name":)",              // Cut after colon
      R"([{"name": "test"})",     // Wrong type + truncated
  };

  for (const auto& payload : truncated) {
    auto [status, body] = client_->post_raw("/v1/agents", payload);
    EXPECT_NE(status, 500) << "500 on truncated JSON: " << payload;
  }

  // NOLINTNEXTLINE(readability-implicit-bool-conversion)
  EXPECT_TRUE(client_->is_healthy()) << "Server crashed after truncated JSON payloads";
}

// E07: Oversized payload (5MB)
TEST_F(PayloadFuzzingTest, E07OversizedPayload) {
  // NOLINTNEXTLINE(bugprone-implicit-widening-of-multiplication-result,bugprone-unused-local-non-trivial-variable)
  const std::string large_value(5 * 1024 * 1024, 'A');
  const nlohmann::json oversized = {{"name", large_value}};

  auto [status, body] = client_->post_raw("/v1/agents", oversized.dump());
  // 413 (too large), 400 (parse error), or 0 (connection reset) — all acceptable
  // NOLINTNEXTLINE(readability-implicit-bool-conversion)
  EXPECT_NE(status, 500) << "Server returned 500 on 5MB payload";

  // Server should still be alive
  // NOLINTNEXTLINE(readability-implicit-bool-conversion)
  EXPECT_TRUE(client_->is_healthy()) << "Server crashed after 5MB payload";
}

// E14: Valid JSON but missing required fields
TEST_F(PayloadFuzzingTest, E14MissingRequiredFields) {
  const std::vector<nlohmann::json> payloads = {
      nlohmann::json::object(),         // Empty object
      {{"irrelevant_field", "value"}},  // No name
      {{"name", nullptr}},              // Null name
      {{"name", 12345}},                // Wrong type for name
      {{"name", ""}},                   // Empty string name
  };

  for (const auto& payload : payloads) {
    auto [status, body] = client_->post("/v1/agents", payload);
    // Graceful handling — 2xx with defaults or 4xx rejection
    EXPECT_NE(status, 500) << "500 on payload: " << payload.dump();
  }

  // NOLINTNEXTLINE(readability-implicit-bool-conversion)
  EXPECT_TRUE(client_->is_healthy());
}

// E15: Valid JSON with extra unknown fields
TEST_F(PayloadFuzzingTest, E15ExtraUnknownFields) {
  const nlohmann::json payload = {
      {"name", "fuzz-agent-" + random_suffix()},
      {"label", "Fuzz Agent"},
      {"program", "none"},
      {"workingDirectory", "/tmp"},
      {"taskDescription", "fuzz test"},
      {"tags", nlohmann::json::array({"fuzz"})},
      {"owner", "e2e"},
      {"role", "member"},
      // Extra fields that Agamemnon doesn't expect
      {"unknown_field_1", "should be ignored"},
      {"__proto__", "injection attempt"},
      {"constructor", {{"prototype", "exploit"}}},
      {"deeply", {{"nested", {{"unknown", true}}}}},
  };

  auto [status, body] = client_->post("/v1/agents", payload);
  // Should create successfully, ignoring unknown fields
  EXPECT_GE(status, 200);
  // NOLINTNEXTLINE(readability-implicit-bool-conversion)
  EXPECT_LT(status, 300) << "Extra fields should be ignored, not cause errors";

  // NOLINTNEXTLINE(readability-implicit-bool-conversion)
  EXPECT_TRUE(client_->is_healthy());
}

}  // namespace charybdis
