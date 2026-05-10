/**
 * @file test_chaos_api.cpp
 * @brief E01-E04: Chaos fault injection API CRUD lifecycle via Agamemnon REST
 *
 * Requires: Agamemnon running at AGAMEMNON_URL (default http://localhost:8080)
 */

#include "projectcharybdis/http_test_client.hpp"
#include "projectcharybdis/test_helpers.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <tuple>

#include <gtest/gtest.h>

namespace projectcharybdis {

// NOLINTNEXTLINE(misc-use-internal-linkage)
class ChaosApiTest : public ::testing::Test {
 protected:
  void SetUp() override {
    client_ = std::make_unique<HttpTestClient>(agamemnon_url());
    // NOLINTNEXTLINE(readability-implicit-bool-conversion)
    if (!client_->is_healthy()) {
      GTEST_SKIP() << "Agamemnon not reachable at " << agamemnon_url();
    }
  }

  // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
  bool fault_in_list(const std::string& fault_id) {
    auto [list_status, list_body] = client_->get("/v1/chaos");
    if (list_status != 200) {
      return false;
    }
    const auto faults = list_body.value("faults", nlohmann::json::array());
    return std::any_of(faults.begin(), faults.end(), [&fault_id](const auto& fault) {
      return fault.value("id", "") == fault_id;
    });
  }

  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
  std::unique_ptr<HttpTestClient> client_;
};

// E01: Inject network-partition fault
TEST_F(ChaosApiTest, E01InjectNetworkPartition) {
  auto [status, body] = client_->post("/v1/chaos/network-partition");
  ASSERT_GE(status, 200);
  ASSERT_LT(status, 300);
  ASSERT_TRUE(body.contains("id"));
  ASSERT_EQ(body.value("type", ""), "network-partition");
  ASSERT_TRUE(body.value("active", false));

  // NOLINTNEXTLINE(bugprone-unused-local-non-trivial-variable)
  const std::string fault_id = body.value("id", "");
  // NOLINTNEXTLINE(readability-implicit-bool-conversion)
  EXPECT_TRUE(fault_in_list(fault_id)) << "Fault not found in GET /v1/chaos";

  std::ignore = client_->del("/v1/chaos/" + fault_id);
}

// E02: Inject latency fault
TEST_F(ChaosApiTest, E02InjectLatency) {
  auto [status, body] = client_->post("/v1/chaos/latency");
  ASSERT_GE(status, 200);
  ASSERT_LT(status, 300);
  EXPECT_EQ(body.value("type", ""), "latency");

  std::ignore = client_->del("/v1/chaos/" + body.value("id", ""));
}

// E03: Inject kill fault
TEST_F(ChaosApiTest, E03InjectKill) {
  auto [status, body] = client_->post("/v1/chaos/kill");
  ASSERT_GE(status, 200);
  ASSERT_LT(status, 300);
  EXPECT_EQ(body.value("type", ""), "kill");

  std::ignore = client_->del("/v1/chaos/" + body.value("id", ""));
}

// E04: Remove fault
TEST_F(ChaosApiTest, E04RemoveFault) {
  auto [status, body] = client_->post("/v1/chaos/queue-starve");
  ASSERT_GE(status, 200);
  const std::string fault_id = body.value("id", "");
  ASSERT_FALSE(fault_id.empty());

  auto [del_status, del_body] = client_->del("/v1/chaos/" + fault_id);
  EXPECT_GE(del_status, 200);
  EXPECT_LT(del_status, 300);

  auto [list_status, list_body] = client_->get("/v1/chaos");
  const auto faults = list_body.value("faults", nlohmann::json::array());
  for (const auto& fault : faults) {
    EXPECT_NE(fault.value("id", ""), fault_id) << "Fault should have been removed";
  }
}

}  // namespace projectcharybdis
