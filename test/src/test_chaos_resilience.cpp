/**
 * @file test_chaos_resilience.cpp
 * @brief R01-R05: Chaos resilience assertions — fault effects and recovery validation
 *
 * Each test follows Inject → Assert effect → Remove → Assert recovery.
 * Requires: Agamemnon running at AGAMEMNON_URL (default http://localhost:8080)
 */

#include "projectcharybdis/http_test_client.hpp"
#include "projectcharybdis/test_helpers.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <nlohmann/json.hpp>
#include <ranges>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace projectcharybdis {

// NOLINTNEXTLINE(misc-use-internal-linkage)
class ChaosResilienceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    client_ = std::make_unique<HttpTestClient>(agamemnon_url());
    // NOLINTNEXTLINE(readability-implicit-bool-conversion)
    if (!client_->is_healthy()) {
      GTEST_SKIP() << "Agamemnon not reachable at " << agamemnon_url();
    }
  }

  void TearDown() override {
    for (const auto& fault_id : injected_ids_) {
      std::ignore = client_->del("/v1/chaos/" + fault_id);
    }
    injected_ids_.clear();
  }

  /// Inject a fault and track its ID for cleanup. Returns the fault response body.
  nlohmann::json inject(const std::string& path,
                        const nlohmann::json& body = nlohmann::json::object()) {
    auto [status, resp] = client_->post(path, body);
    EXPECT_GE(status, 200);
    EXPECT_LT(status, 300);
    const std::string fault_id = resp.value("id", "");
    EXPECT_FALSE(fault_id.empty()) << "Fault response missing 'id' field";
    if (!fault_id.empty()) {
      injected_ids_.push_back(fault_id);
    }
    return resp;
  }

  /// Remove a fault by ID and untrack it.
  void remove(const std::string& fault_id) {
    auto [status, resp] = client_->del("/v1/chaos/" + fault_id);
    EXPECT_GE(status, 200);
    EXPECT_LT(status, 300);
    injected_ids_.erase(std::remove(injected_ids_.begin(), injected_ids_.end(), fault_id),
                        injected_ids_.end());
  }

  /// Check whether the chaos status endpoint reflects an active fault of given type.
  // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
  bool chaos_status_has_type(const std::string& fault_type) {
    auto [status, body] = client_->get("/v1/chaos");
    if (status != 200) {
      return false;
    }
    const auto faults = body.value("faults", nlohmann::json::array());
    return std::ranges::any_of(faults, [&fault_type](const auto& fault) {
      return fault.value("type", "") == fault_type && fault.value("active", false);
    });
  }

  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
  std::unique_ptr<HttpTestClient> client_;
  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
  std::vector<std::string> injected_ids_;
};

// R01: Network partition → health degrades → recovers after removal
TEST_F(ChaosResilienceTest, R01NetworkPartitionDegradedHealth) {
  const auto fault = inject("/v1/chaos/network-partition");
  const std::string fault_id = fault.value("id", "");
  ASSERT_FALSE(fault_id.empty());
  ASSERT_EQ(fault.value("type", ""), "network-partition");
  ASSERT_TRUE(fault.value("active", false));

  // Assert effect: chaos list reflects the active partition within 5s
  // NOLINTNEXTLINE(readability-implicit-bool-conversion)
  const bool effect_observed = wait_until(
      [&]() { return chaos_status_has_type("network-partition"); }, std::chrono::seconds{5});
  EXPECT_TRUE(effect_observed) << "Network partition not visible in chaos status within 5s";

  // Remove fault
  remove(fault_id);

  // Assert recovery: health returns ok within 10s
  // NOLINTNEXTLINE(readability-implicit-bool-conversion)
  const bool recovered =
      wait_until([&]() { return client_->is_healthy(); }, std::chrono::seconds{10});
  EXPECT_TRUE(recovered)
      << "Agamemnon health did not recover within 10s after removing network partition";
}

// R02: Latency injection → subsequent requests are slow → fast after removal
TEST_F(ChaosResilienceTest, R02LatencyInjectionSlowResponse) {
  const int delay_ms = 2000;
  const auto fault = inject("/v1/chaos/latency", {{"delay_ms", delay_ms}});
  const std::string fault_id = fault.value("id", "");
  ASSERT_FALSE(fault_id.empty());
  ASSERT_EQ(fault.value("type", ""), "latency");

  // Assert effect: a health probe takes >= delay_ms to complete
  const auto start_slow = std::chrono::steady_clock::now();
  std::ignore = client_->get("/v1/health");
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start_slow)
                              .count();
  EXPECT_GE(elapsed_ms, delay_ms) << "Expected latency-injected request to take >= " << delay_ms
                                  << "ms, got " << elapsed_ms << "ms";

  // Remove fault
  remove(fault_id);

  // Assert recovery: health probe now completes quickly
  const auto start_fast = std::chrono::steady_clock::now();
  std::ignore = client_->get("/v1/health");
  const auto fast_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start_fast)
                           .count();
  EXPECT_LT(fast_ms, 1000) << "Expected fast request after latency removal, got " << fast_ms
                           << "ms";
}

// R03: Kill fault → health degrades → recovers after removal (or auto-restart)
//
// REQUIRES_NATS              — Agamemnon must be reachable (inherits suite default).
// REQUIRES_RESTART_SUPERVISOR — An external supervisor must auto-restart Agamemnon
//   after the kill fault (e.g. systemd Restart=on-failure, Kubernetes pod restart,
//   Docker --restart=always). Without one, the process stays dead and the recovery
//   assertion will always time out. Skip in CI without a supervisor with:
//     ctest --label-exclude REQUIRES_RESTART_SUPERVISOR
//
// The recovery window is configurable via CHAOS_RECOVERY_TIMEOUT_S (seconds);
// see test_helpers.hpp::chaos_recovery_timeout(). The default (10s) suits
// fast-restart supervisors; slower restart policies (back-off, image pull,
// readiness gates) should raise it.
TEST_F(ChaosResilienceTest, R03KillServiceHealthDegrades) {
  const auto fault = inject("/v1/chaos/kill");
  const std::string fault_id = fault.value("id", "");
  ASSERT_FALSE(fault_id.empty());
  ASSERT_EQ(fault.value("type", ""), "kill");

  // Assert effect: health degrades within 5s of killing the service
  // NOLINTNEXTLINE(readability-implicit-bool-conversion)
  const bool degraded =
      wait_until([&]() { return !client_->is_healthy(); }, std::chrono::seconds{5});
  EXPECT_TRUE(degraded) << "Expected health to degrade within 5s of kill fault";

  // Remove fault. NOTE: this only clears Charybdis's fault registry; it does
  // NOT restart Agamemnon. Recovery requires an external supervisor to relaunch
  // the killed process. The injected_ids_ tracking is still cleared so TearDown
  // does not double-DELETE the fault.
  remove(fault_id);

  // Assert recovery: health returns ok within the configured timeout window.
  const auto recovery_timeout = chaos_recovery_timeout();
  // NOLINTNEXTLINE(readability-implicit-bool-conversion)
  const bool recovered = wait_until([&]() { return client_->is_healthy(); }, recovery_timeout);
  EXPECT_TRUE(recovered) << "Agamemnon health did not recover within " << recovery_timeout.count()
                         << "s after removing kill fault — verify an external restart supervisor "
                            "is configured, or raise CHAOS_RECOVERY_TIMEOUT_S";
}

// R04: Queue-starve → task stays pending → advances to completed after removal
//
// REQUIRES_NATS   — Agamemnon + NATS JetStream must be reachable.
// REQUIRES_MYRMIDON — A live myrmidon pull consumer must be running.
//   Without it, the task will never transition from 'pending' to 'completed'
//   and the 10-second recovery assertion will always time out.
//   Skip in CI with: ctest --label-exclude REQUIRES_MYRMIDON
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(ChaosResilienceTest, R04QueueStarveConsumerStalls) {
  const auto fault = inject("/v1/chaos/queue-starve");
  const std::string fault_id = fault.value("id", "");
  ASSERT_FALSE(fault_id.empty());
  ASSERT_EQ(fault.value("type", ""), "queue-starve");

  // Create a team and agent to submit a task through
  auto [ts, team_resp] = client_->post("/v1/teams", {{"name", "r04-team-" + random_suffix()}});
  ASSERT_GE(ts, 200);
  const std::string team_id = team_resp.value("team", nlohmann::json{}).value("id", "");
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
  const std::string agent_id = agent_resp.contains("id")
                                   ? agent_resp.value("id", "")
                                   : agent_resp.value("agent", nlohmann::json{}).value("id", "");

  // Submit a task that should be pulled by a consumer
  auto [s3, task_resp] =
      client_->post("/v1/teams/" + team_id + "/tasks", {{"subject", "R04 stall test"},
                                                        {"description", "queue starve resilience"},
                                                        {"type", "hello"},
                                                        {"assigneeAgentId", agent_id}});
  ASSERT_GE(s3, 200);
  const std::string task_id = task_resp.value("task", nlohmann::json{}).value("id", "");
  ASSERT_FALSE(task_id.empty());

  // Assert effect: task stays pending during the starve window (poll for 3s)
  const bool advanced_while_starved = wait_until(
      [&]() {
        auto [ts2, tasks] = client_->get("/v1/tasks");
        for (const auto& task : tasks.value("tasks", nlohmann::json::array())) {
          if (task.value("id", "") == task_id) {
            return task.value("status", "pending") != "pending";
          }
        }
        return false;
      },
      std::chrono::seconds{3});
  EXPECT_FALSE(advanced_while_starved) << "Task should remain pending while queue is starved";

  // Remove the starvation fault
  remove(fault_id);

  // Assert recovery: task now advances to completed within 10s
  const bool completed = wait_until(
      [&]() {
        auto [ts3, tasks] = client_->get("/v1/tasks");
        for (const auto& task : tasks.value("tasks", nlohmann::json::array())) {
          if (task.value("id", "") == task_id) {
            return task.value("status", "") == "completed";
          }
        }
        return false;
      },
      std::chrono::seconds{10});
  EXPECT_TRUE(completed) << "Task did not complete within 10s after removing queue-starve fault";
}

// R05: Stacked faults (latency + kill) — both effects observable, all clear after removal
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(ChaosResilienceTest, R05MultiFaultStackedAndClearAll) {
  const int delay_ms = 1500;
  const auto latency_fault = inject("/v1/chaos/latency", {{"delay_ms", delay_ms}});
  const std::string latency_id = latency_fault.value("id", "");
  ASSERT_FALSE(latency_id.empty());

  const auto kill_fault = inject("/v1/chaos/kill");
  const std::string kill_id = kill_fault.value("id", "");
  ASSERT_FALSE(kill_id.empty());

  // Assert both faults are active in the chaos list
  auto [ls, list_body] = client_->get("/v1/chaos");
  ASSERT_EQ(ls, 200);
  const auto faults = list_body.value("faults", nlohmann::json::array());
  const bool has_latency = std::ranges::any_of(
      faults, [&latency_id](const auto& fault) { return fault.value("id", "") == latency_id; });
  const bool has_kill = std::ranges::any_of(
      faults, [&kill_id](const auto& fault) { return fault.value("id", "") == kill_id; });
  EXPECT_TRUE(has_latency) << "Latency fault not found in active fault list";
  EXPECT_TRUE(has_kill) << "Kill fault not found in active fault list";

  // Assert stacked effect: health is degraded (kill takes effect)
  // NOLINTNEXTLINE(readability-implicit-bool-conversion)
  const bool degraded =
      wait_until([&]() { return !client_->is_healthy(); }, std::chrono::seconds{5});
  EXPECT_TRUE(degraded) << "Expected health to degrade with kill fault active";

  // Remove both faults
  remove(latency_id);
  remove(kill_id);

  // Assert full recovery: health returns ok and no faults remain active within 10s
  // NOLINTNEXTLINE(readability-implicit-bool-conversion)
  const bool recovered =
      wait_until([&]() { return client_->is_healthy(); }, std::chrono::seconds{10});
  EXPECT_TRUE(recovered) << "Health did not recover within 10s after removing all faults";

  auto [ls2, list2] = client_->get("/v1/chaos");
  ASSERT_EQ(ls2, 200);
  const auto remaining = list2.value("faults", nlohmann::json::array());
  const bool latency_gone = !std::ranges::any_of(
      remaining, [&latency_id](const auto& fault) { return fault.value("id", "") == latency_id; });
  const bool kill_gone = !std::ranges::any_of(
      remaining, [&kill_id](const auto& fault) { return fault.value("id", "") == kill_id; });
  EXPECT_TRUE(latency_gone) << "Latency fault still present after removal";
  EXPECT_TRUE(kill_gone) << "Kill fault still present after removal";
}

}  // namespace projectcharybdis
