/**
 * @file test_malformed_messages.cpp
 * @brief D11/D12 variant: Malformed REST payloads via C++ client
 *
 * Tests that Agamemnon gracefully handles garbage in REST API bodies.
 * The NATS-level malformed message tests are in the bash E2E scripts.
 *
 * Requires: Agamemnon running at AGAMEMNON_URL
 */

#include "projectcharybdis/http_test_client.hpp"
#include "projectcharybdis/test_helpers.hpp"

#include <memory>
#include <nlohmann/json.hpp>
#include <string>

#include <gtest/gtest.h>

namespace projectcharybdis {

// NOLINTNEXTLINE(misc-use-internal-linkage)
class MalformedMessageTest : public ::testing::Test {
 protected:
  void SetUp() override {
    client_ = std::make_unique<HttpTestClient>(agamemnon_url());
    // NOLINTNEXTLINE(readability-implicit-bool-conversion)
    if (!client_->is_healthy()) {
      GTEST_SKIP() << "Agamemnon not reachable";
    }
  }

  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
  std::unique_ptr<HttpTestClient> client_;
};

// D11 variant: malformed body to task completion endpoint (REST-level)
TEST_F(MalformedMessageTest, MalformedTaskCreation) {
  // Non-JSON to task creation
  auto [s1, _1] = client_->post_raw("/v1/teams/fake/tasks", "not json");
  EXPECT_GE(s1, 400);

  // Binary garbage
  std::string binary(256, '\0');
  for (int i = 0; i < 256; ++i) {
    binary[i] = static_cast<char>(i);
  }
  auto [s2, _2] = client_->post_raw("/v1/agents", binary);
  EXPECT_NE(s2, 500);

  // XML instead of JSON
  auto [s3, _3] = client_->post_raw("/v1/agents", "<agent><name>xml</name></agent>", "text/xml");
  EXPECT_NE(s3, 500);

  // NOLINTNEXTLINE(readability-implicit-bool-conversion)
  EXPECT_TRUE(client_->is_healthy());
}

// Empty bodies on various endpoints
TEST_F(MalformedMessageTest, EmptyBodies) {
  auto [s1, _1] = client_->post_raw("/v1/agents", "");
  EXPECT_NE(s1, 500);

  auto [s2, _2] = client_->post_raw("/v1/teams", "");
  EXPECT_NE(s2, 500);

  auto [s3, _3] = client_->post_raw("/v1/chaos/test-empty", "");
  EXPECT_NE(s3, 500);

  // NOLINTNEXTLINE(readability-implicit-bool-conversion)
  EXPECT_TRUE(client_->is_healthy());
}

// /v1/chaos/test-empty is a diagnostic endpoint and must reject non-empty
// bodies with 400 — it must never create a 'test-empty' fault record.
// Regression coverage for issue #89.
TEST_F(MalformedMessageTest, ChaosTestEmptyRejectsNonEmptyBody) {
  // Empty body: documented contract is 400.
  auto [empty_status, _e] = client_->post_raw("/v1/chaos/test-empty", "");
  EXPECT_EQ(empty_status, 400);

  // Non-empty JSON: must also be rejected (no fault record created).
  auto [json_status, _j] = client_->post_raw("/v1/chaos/test-empty", R"({"active":true})");
  EXPECT_EQ(json_status, 400);

  // Non-empty plain text: same rejection.
  auto [text_status, _t] = client_->post_raw("/v1/chaos/test-empty", "anything", "text/plain");
  EXPECT_EQ(text_status, 400);

  // NOLINTNEXTLINE(readability-implicit-bool-conversion)
  EXPECT_TRUE(client_->is_healthy());
}

// Wrong content-type header
TEST_F(MalformedMessageTest, WrongContentType) {
  const nlohmann::json valid = {{"name", "wrong-ct"}};
  auto [status, body] = client_->post_raw("/v1/agents", valid.dump(), "text/plain");
  // Should either parse it anyway or reject gracefully
  EXPECT_NE(status, 500);
  // NOLINTNEXTLINE(readability-implicit-bool-conversion)
  EXPECT_TRUE(client_->is_healthy());
}

}  // namespace projectcharybdis
