/**
 * @file test_chaos_api.cpp
 * @brief E01-E04: Chaos fault injection API CRUD lifecycle via Agamemnon REST
 *
 * Requires: Agamemnon running at AGAMEMNON_URL (default http://localhost:8080)
 */

#include "projectcharybdis/http_test_client.hpp"
#include "projectcharybdis/test_helpers.hpp"

#include <gtest/gtest.h>

namespace projectcharybdis {

class ChaosApiTest : public ::testing::Test {
 protected:
  void SetUp() override {
    client_ = std::make_unique<HttpTestClient>(agamemnon_url());
    if (!client_->is_healthy()) {
      GTEST_SKIP() << "Agamemnon not reachable at " << agamemnon_url();
    }
  }

  std::unique_ptr<HttpTestClient> client_;
};

// E01: Inject network-partition fault
TEST_F(ChaosApiTest, E01_InjectNetworkPartition) {
  auto [status, body] = client_->post("/v1/chaos/network-partition");
  ASSERT_GE(status, 200);
  ASSERT_LT(status, 300);
  ASSERT_TRUE(body.contains("id"));
  ASSERT_EQ(body.value("type", ""), "network-partition");
  ASSERT_TRUE(body.value("active", false));

  // Verify in list
  auto [list_status, list_body] = client_->get("/v1/chaos");
  ASSERT_EQ(list_status, 200);
  auto faults = list_body.value("faults", nlohmann::json::array());
  bool found = false;
  for (const auto& f : faults) {
    if (f.value("id", "") == body.value("id", "")) {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found) << "Fault not found in GET /v1/chaos";

  // Cleanup
  client_->del("/v1/chaos/" + body.value("id", ""));
}

// E02: Inject latency fault
TEST_F(ChaosApiTest, E02_InjectLatency) {
  auto [status, body] = client_->post("/v1/chaos/latency");
  ASSERT_GE(status, 200);
  ASSERT_LT(status, 300);
  EXPECT_EQ(body.value("type", ""), "latency");

  // Cleanup
  client_->del("/v1/chaos/" + body.value("id", ""));
}

// E03: Inject kill fault
TEST_F(ChaosApiTest, E03_InjectKill) {
  auto [status, body] = client_->post("/v1/chaos/kill");
  ASSERT_GE(status, 200);
  ASSERT_LT(status, 300);
  EXPECT_EQ(body.value("type", ""), "kill");

  // Cleanup
  client_->del("/v1/chaos/" + body.value("id", ""));
}

// E04: Remove fault
TEST_F(ChaosApiTest, E04_RemoveFault) {
  // Inject
  auto [status, body] = client_->post("/v1/chaos/queue-starve");
  ASSERT_GE(status, 200);
  std::string fault_id = body.value("id", "");
  ASSERT_FALSE(fault_id.empty());

  // Remove
  auto [del_status, del_body] = client_->del("/v1/chaos/" + fault_id);
  EXPECT_GE(del_status, 200);
  EXPECT_LT(del_status, 300);

  // Verify removed
  auto [list_status, list_body] = client_->get("/v1/chaos");
  auto faults = list_body.value("faults", nlohmann::json::array());
  for (const auto& f : faults) {
    EXPECT_NE(f.value("id", ""), fault_id) << "Fault should have been removed";
  }
}

}  // namespace projectcharybdis
