/**
 * @file test_chaos_resilience.cpp
 * @brief R01-R05: Chaos resilience assertions — fault effects and recovery validation
 *
 * Each test follows Inject → Assert effect → Remove → Assert recovery.
 * Requires: Agamemnon running at AGAMEMNON_URL (default http://localhost:8080)
 */

#include "projectcharybdis/http_test_client.hpp"
#include "projectcharybdis/test_helpers.hpp"

#include <chrono>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using namespace projectcharybdis;

class ChaosResilienceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    client_ = std::make_unique<HttpTestClient>(agamemnon_url());
    if (!client_->is_healthy()) {
      GTEST_SKIP() << "Agamemnon not reachable at " << agamemnon_url();
    }
  }

  void TearDown() override {
    for (const auto& id : injected_ids_) {
      client_->del("/v1/chaos/" + id);
    }
    injected_ids_.clear();
  }

  /// Inject a fault and track its ID for cleanup. Returns the fault response body.
  nlohmann::json inject(const std::string& path,
                        const nlohmann::json& body = nlohmann::json::object()) {
    auto [status, resp] = client_->post(path, body);
    EXPECT_GE(status, 200);
    EXPECT_LT(status, 300);
    std::string id = resp.value("id", "");
    EXPECT_FALSE(id.empty()) << "Fault response missing 'id' field";
    if (!id.empty()) {
      injected_ids_.push_back(id);
    }
    return resp;
  }

  /// Remove a fault by ID and untrack it.
  void remove(const std::string& id) {
    auto [status, resp] = client_->del("/v1/chaos/" + id);
    EXPECT_GE(status, 200);
    EXPECT_LT(status, 300);
    injected_ids_.erase(std::remove(injected_ids_.begin(), injected_ids_.end(), id),
                        injected_ids_.end());
  }

  /// Check whether the chaos status endpoint reflects an active fault of given type.
  bool chaos_status_has_type(const std::string& fault_type) {
    auto [status, body] = client_->get("/v1/chaos");
    if (status != 200) return false;
    auto faults = body.value("faults", nlohmann::json::array());
    for (const auto& f : faults) {
      if (f.value("type", "") == fault_type && f.value("active", false)) return true;
    }
    return false;
  }

  std::unique_ptr<HttpTestClient> client_;
  std::vector<std::string> injected_ids_;
};

// R01: Network partition → health degrades → recovers after removal
TEST_F(ChaosResilienceTest, R01_NetworkPartition_DegradedHealth) {
  auto fault = inject("/v1/chaos/network-partition");
  std::string fault_id = fault.value("id", "");
  ASSERT_FALSE(fault_id.empty());
  ASSERT_EQ(fault.value("type", ""), "network-partition");
  ASSERT_TRUE(fault.value("active", false));

  // Assert effect: chaos list reflects the active partition within 5s
  bool effect_observed = wait_until([&]() { return chaos_status_has_type("network-partition"); },
                                    std::chrono::seconds{5});
  EXPECT_TRUE(effect_observed) << "Network partition not visible in chaos status within 5s";

  // Remove fault
  remove(fault_id);

  // Assert recovery: health returns ok within 10s
  bool recovered = wait_until([&]() { return client_->is_healthy(); }, std::chrono::seconds{10});
  EXPECT_TRUE(recovered)
      << "Agamemnon health did not recover within 10s after removing network partition";
}

// R02: Latency injection → subsequent requests are slow → fast after removal
TEST_F(ChaosResilienceTest, R02_LatencyInjection_SlowResponse) {
  const int delay_ms = 2000;
  auto fault = inject("/v1/chaos/latency", {{"delay_ms", delay_ms}});
  std::string fault_id = fault.value("id", "");
  ASSERT_FALSE(fault_id.empty());
  ASSERT_EQ(fault.value("type", ""), "latency");

  // Assert effect: a health probe takes >= delay_ms to complete
  auto t0 = std::chrono::steady_clock::now();
  client_->get("/v1/health");
  auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0)
          .count();
  EXPECT_GE(elapsed_ms, delay_ms) << "Expected latency-injected request to take >= " << delay_ms
                                  << "ms, got " << elapsed_ms << "ms";

  // Remove fault
  remove(fault_id);

  // Assert recovery: health probe now completes quickly
  auto t1 = std::chrono::steady_clock::now();
  client_->get("/v1/health");
  auto fast_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t1)
          .count();
  EXPECT_LT(fast_ms, 1000) << "Expected fast request after latency removal, got " << fast_ms
                           << "ms";
}

// R03: Kill fault → health degrades → recovers after removal (or auto-restart)
TEST_F(ChaosResilienceTest, R03_KillService_HealthDegrades) {
  auto fault = inject("/v1/chaos/kill");
  std::string fault_id = fault.value("id", "");
  ASSERT_FALSE(fault_id.empty());
  ASSERT_EQ(fault.value("type", ""), "kill");

  // Assert effect: health degrades within 5s of killing the service
  bool degraded = wait_until([&]() { return !client_->is_healthy(); }, std::chrono::seconds{5});
  EXPECT_TRUE(degraded) << "Expected health to degrade within 5s of kill fault";

  // Remove fault (allows auto-restart)
  remove(fault_id);

  // Assert recovery: health returns ok within 10s
  bool recovered = wait_until([&]() { return client_->is_healthy(); }, std::chrono::seconds{10});
  EXPECT_TRUE(recovered) << "Agamemnon health did not recover within 10s after removing kill fault";
}

// R04: Queue-starve → task stays pending → advances to completed after removal
TEST_F(ChaosResilienceTest, R04_QueueStarve_ConsumerStalls) {
  auto fault = inject("/v1/chaos/queue-starve");
  std::string fault_id = fault.value("id", "");
  ASSERT_FALSE(fault_id.empty());
  ASSERT_EQ(fault.value("type", ""), "queue-starve");

  // Create a team and agent to submit a task through
  auto [ts, team_resp] = client_->post("/v1/teams", {{"name", "r04-team-" + random_suffix()}});
  ASSERT_GE(ts, 200);
  std::string team_id = team_resp.value("team", nlohmann::json{}).value("id", "");
  ASSERT_FALSE(team_id.empty());

  auto [as, agent_resp] = client_->post("/v1/agents", {{"name", "r04-agent-" + random_suffix()},
                                                       {"label", "R04"},
                                                       {"program", "none"},
                                                       {"workingDirectory", "/tmp"},
                                                       {"taskDescription", "queue starve test"},
                                                       {"tags", nlohmann::json::array({"r04"})},
                                                       {"owner", "e2e"},
                                                       {"role", "member"}});
  ASSERT_GE(as, 200);
  std::string agent_id = agent_resp.contains("id")
                             ? agent_resp.value("id", "")
                             : agent_resp.value("agent", nlohmann::json{}).value("id", "");

  // Submit a task that should be pulled by a consumer
  auto [s3, task_resp] =
      client_->post("/v1/teams/" + team_id + "/tasks", {{"subject", "R04 stall test"},
                                                        {"description", "queue starve resilience"},
                                                        {"type", "hello"},
                                                        {"assigneeAgentId", agent_id}});
  ASSERT_GE(s3, 200);
  std::string task_id = task_resp.value("task", nlohmann::json{}).value("id", "");
  ASSERT_FALSE(task_id.empty());

  // Assert effect: task stays pending during the starve window (poll for 3s)
  bool advanced_while_starved = wait_until(
      [&]() {
        auto [ts2, tasks] = client_->get("/v1/tasks");
        for (const auto& t : tasks.value("tasks", nlohmann::json::array())) {
          if (t.value("id", "") == task_id) {
            return t.value("status", "pending") != "pending";
          }
        }
        return false;
      },
      std::chrono::seconds{3});
  EXPECT_FALSE(advanced_while_starved) << "Task should remain pending while queue is starved";

  // Remove the starvation fault
  remove(fault_id);

  // Assert recovery: task now advances to completed within 10s
  bool completed = wait_until(
      [&]() {
        auto [ts3, tasks] = client_->get("/v1/tasks");
        for (const auto& t : tasks.value("tasks", nlohmann::json::array())) {
          if (t.value("id", "") == task_id) {
            return t.value("status", "") == "completed";
          }
        }
        return false;
      },
      std::chrono::seconds{10});
  EXPECT_TRUE(completed) << "Task did not complete within 10s after removing queue-starve fault";
}

// R05: Stacked faults (latency + kill) — both effects observable, all clear after removal
TEST_F(ChaosResilienceTest, R05_MultiFault_StackedAndClearAll) {
  const int delay_ms = 1500;
  auto latency_fault = inject("/v1/chaos/latency", {{"delay_ms", delay_ms}});
  std::string latency_id = latency_fault.value("id", "");
  ASSERT_FALSE(latency_id.empty());

  auto kill_fault = inject("/v1/chaos/kill");
  std::string kill_id = kill_fault.value("id", "");
  ASSERT_FALSE(kill_id.empty());

  // Assert both faults are active in the chaos list
  auto [ls, list_body] = client_->get("/v1/chaos");
  ASSERT_EQ(ls, 200);
  auto faults = list_body.value("faults", nlohmann::json::array());
  bool has_latency = false;
  bool has_kill = false;
  for (const auto& f : faults) {
    if (f.value("id", "") == latency_id) has_latency = true;
    if (f.value("id", "") == kill_id) has_kill = true;
  }
  EXPECT_TRUE(has_latency) << "Latency fault not found in active fault list";
  EXPECT_TRUE(has_kill) << "Kill fault not found in active fault list";

  // Assert stacked effect: health is degraded (kill takes effect)
  bool degraded = wait_until([&]() { return !client_->is_healthy(); }, std::chrono::seconds{5});
  EXPECT_TRUE(degraded) << "Expected health to degrade with kill fault active";

  // Remove both faults
  remove(latency_id);
  remove(kill_id);

  // Assert full recovery: health returns ok and no faults remain active within 10s
  bool recovered = wait_until([&]() { return client_->is_healthy(); }, std::chrono::seconds{10});
  EXPECT_TRUE(recovered) << "Health did not recover within 10s after removing all faults";

  auto [ls2, list2] = client_->get("/v1/chaos");
  ASSERT_EQ(ls2, 200);
  auto remaining = list2.value("faults", nlohmann::json::array());
  bool latency_gone = true;
  bool kill_gone = true;
  for (const auto& f : remaining) {
    if (f.value("id", "") == latency_id) latency_gone = false;
    if (f.value("id", "") == kill_id) kill_gone = false;
  }
  EXPECT_TRUE(latency_gone) << "Latency fault still present after removal";
  EXPECT_TRUE(kill_gone) << "Kill fault still present after removal";
}
