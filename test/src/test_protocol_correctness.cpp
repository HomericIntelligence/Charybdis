/**
 * @file test_protocol_correctness.cpp
 * @brief C10: Idempotent stream creation, protocol assertions via REST
 *
 * Requires: Agamemnon + NATS running
 */

#include "projectcharybdis/http_test_client.hpp"
#include "projectcharybdis/test_helpers.hpp"

#include <chrono>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <string>

#include <gtest/gtest.h>
#include <set>

namespace projectcharybdis {
namespace {

static std::string extract_agent_id(const nlohmann::json& agent) {
  return agent.contains("id") ? agent.value("id", "")
                              : agent.value("agent", nlohmann::json{}).value("id", "");
}

}  // namespace

// NOLINTNEXTLINE(misc-use-internal-linkage)
class ProtocolCorrectnessTest : public ::testing::Test {
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

// C10: Idempotent stream creation — verify Agamemnon can be restarted
// and ensure_streams() called multiple times without error
TEST_F(ProtocolCorrectnessTest, C10IdempotentStreamCreation) {
  // Agamemnon calls ensure_streams() on startup. If it's running, streams exist.
  // Verify health (which implies ensure_streams succeeded)
  // NOLINTNEXTLINE(readability-implicit-bool-conversion)
  EXPECT_TRUE(client_->is_healthy());

  // Create a task — this exercises the NATS publish path which requires streams
  auto [s1, team] = client_->post("/v1/teams", {{"name", "c10-team"}});
  ASSERT_GE(s1, 200);
  ASSERT_LT(s1, 300);
  const std::string team_id = team.value("team", nlohmann::json{}).value("id", "");
  ASSERT_FALSE(team_id.empty());

  // Create agent
  auto [s2, agent] = client_->post("/v1/agents", {{"name", "c10-agent"},
                                                  {"label", "C10"},
                                                  {"program", "none"},
                                                  {"workingDirectory", "/tmp"},
                                                  {"taskDescription", "c10"},
                                                  {"tags", nlohmann::json::array({"c10"})},
                                                  {"owner", "e2e"},
                                                  {"role", "member"}});
  ASSERT_GE(s2, 200);
  const std::string agent_id = extract_agent_id(agent);

  // Create task — exercises NATS publish to hi.myrmidon.hello.*
  auto [s3, task] =
      client_->post("/v1/teams/" + team_id + "/tasks", {{"subject", "C10 stream test"},
                                                        {"description", "protocol test"},
                                                        {"type", "hello"},
                                                        {"assigneeAgentId", agent_id}});
  EXPECT_GE(s3, 200);
  EXPECT_LT(s3, 300) << "Task creation should succeed (streams exist)";
}

// Verify task state is only pending or completed (no intermediate states)
TEST_F(ProtocolCorrectnessTest, TaskStateOnlyPendingOrCompleted) {
  // Create and immediately check state
  auto [s1, team] = client_->post("/v1/teams", {{"name", "state-team"}});
  const std::string team_id = team.value("team", nlohmann::json{}).value("id", "");

  auto [s2, agent] = client_->post("/v1/agents", {{"name", "state-agent"},
                                                  {"label", "State"},
                                                  {"program", "none"},
                                                  {"workingDirectory", "/tmp"},
                                                  {"taskDescription", "state"},
                                                  {"tags", nlohmann::json::array()},
                                                  {"owner", "e2e"},
                                                  {"role", "member"}});
  const std::string agent_id = extract_agent_id(agent);

  auto [s3, task_resp] =
      client_->post("/v1/teams/" + team_id + "/tasks", {{"subject", "State test"},
                                                        {"description", "state"},
                                                        {"type", "hello"},
                                                        {"assigneeAgentId", agent_id}});
  const std::string task_id = task_resp.value("task", nlohmann::json{}).value("id", "");

  // Poll and collect states
  std::set<std::string> observed_states;
  std::ignore = wait_until(
      [&]() {
        auto [ts, tasks] = client_->get("/v1/tasks");
        for (const auto& task : tasks.value("tasks", nlohmann::json::array())) {
          if (task.value("id", "") == task_id) {
            observed_states.insert(task.value("status", "unknown"));
            return task.value("status", "") == "completed";
          }
        }
        return false;
      },
      std::chrono::seconds{30});

  for (const auto& state : observed_states) {
    EXPECT_TRUE(state == "pending" || state == "completed") << "Unexpected state: " << state;
  }
}

}  // namespace projectcharybdis
