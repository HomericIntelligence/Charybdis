/**
 * @file test_http_client_timeout_unit.cpp
 * @brief Unit tests for the TimeoutConfig constructor parameter (issue #81).
 *
 * Covers default preservation of the pre-#81 5s/10s behaviour, custom
 * timeout values, validation of negative values, zero ("no timeout")
 * semantics, and a large read timeout for latency-injection fixtures.
 * No live Agamemnon required — uses port 1 (refused) and an in-process
 * mock server.
 */

#include "charybdis/http_test_client.hpp"

#include <chrono>
#include <httplib.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include <gtest/gtest.h>

namespace charybdis {

// ── Defaults preserve legacy behaviour ────────────────────────────────────────

TEST(HttpTestClientTimeoutUnit, DefaultTimeoutsPreserveLegacyBehaviour) {
  // Constructing without the struct must compile and behave as before #81.
  HttpTestClient client("http://127.0.0.1:1");
  EXPECT_FALSE(client.is_healthy());
}

TEST(HttpTestClientTimeoutUnit, CustomTimeoutsAcceptedByConstructor) {
  HttpTestClient client("http://127.0.0.1:1", {}, {},
                        TimeoutConfig{.connection_timeout_sec = 1, .read_timeout_sec = 2});
  auto [status, body] = client.get("/x");
  EXPECT_EQ(status, 0);
  EXPECT_TRUE(body.empty());
}

// ── Negative values are rejected ──────────────────────────────────────────────

TEST(HttpTestClientTimeoutUnit, NegativeConnectionTimeoutThrows) {
  EXPECT_THROW((HttpTestClient("http://127.0.0.1:1", {}, {},
                               TimeoutConfig{.connection_timeout_sec = -1})),
               std::runtime_error);
}

TEST(HttpTestClientTimeoutUnit, NegativeReadTimeoutThrows) {
  EXPECT_THROW(
      (HttpTestClient("http://127.0.0.1:1", {}, {}, TimeoutConfig{.read_timeout_sec = -1})),
      std::runtime_error);
}

// ── Zero means "no timeout" (cpp-httplib semantics) ──────────────────────────

TEST(HttpTestClientTimeoutUnit, ZeroConnectionTimeoutAccepted) {
  HttpTestClient client("http://127.0.0.1:1", {}, {},
                        TimeoutConfig{.connection_timeout_sec = 0});
  auto [status, body] = client.get("/x");
  EXPECT_EQ(status, 0);
  EXPECT_TRUE(body.empty());
}

TEST(HttpTestClientTimeoutUnit, ZeroReadTimeoutAccepted) {
  HttpTestClient client("http://127.0.0.1:1", {}, {},
                        TimeoutConfig{.read_timeout_sec = 0});
  auto [status, body] = client.get("/x");
  EXPECT_EQ(status, 0);
  EXPECT_TRUE(body.empty());
}

// ── Mock server: large timeouts must not regress normal operation ────────────

/// Minimal in-process HTTP mock server for unit tests (mirrors
/// test_http_client_unit.cpp).
// NOLINTNEXTLINE(misc-use-internal-linkage)
class TimeoutMockServer {
 public:
  TimeoutMockServer() {
    port_ = svr_.bind_to_any_port("127.0.0.1");

    svr_.Get("/v1/json", [](const httplib::Request& /*req*/, httplib::Response& res) {
      res.set_content(R"({"key":"value"})", "application/json");
    });

    thread_ = std::thread([this]() { svr_.listen_after_bind(); });

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!svr_.is_running()) {
      if (std::chrono::steady_clock::now() > deadline) {
        throw std::runtime_error("TimeoutMockServer failed to start within 5 seconds");
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
  }

  ~TimeoutMockServer() {
    svr_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  TimeoutMockServer(const TimeoutMockServer&) = delete;
  TimeoutMockServer& operator=(const TimeoutMockServer&) = delete;
  TimeoutMockServer(TimeoutMockServer&&) = delete;
  TimeoutMockServer& operator=(TimeoutMockServer&&) = delete;

  [[nodiscard]] int port() const { return port_; }

 private:
  httplib::Server svr_;
  int port_{0};
  std::thread thread_;
};

TEST(HttpTestClientTimeoutUnit, LongReadTimeoutPropagatedForLatencyInjection) {
  // A latency-injection fixture would use a large read timeout; assert no
  // functional regression at that value against a live in-process server.
  // cpp-httplib does not expose its configured timeout for readback, so this
  // asserts correct behaviour rather than the stored value (same precedent
  // as the client-pointer-stability tests in test_http_client_unit.cpp).
  TimeoutMockServer mock;
  HttpTestClient client("http://127.0.0.1:" + std::to_string(mock.port()), {}, {},
                        TimeoutConfig{.read_timeout_sec = 60});
  auto [status, body] = client.get("/v1/json");
  EXPECT_EQ(status, 200);
  EXPECT_EQ(body.value("key", ""), "value");
}

TEST(HttpTestClientTimeoutUnit, CustomShortReadTimeoutStillServesFastResponses) {
  // Sanity: a short-but-valid read timeout still handles immediate responses.
  TimeoutMockServer mock;
  HttpTestClient client("http://127.0.0.1:" + std::to_string(mock.port()), {}, {},
                        TimeoutConfig{.connection_timeout_sec = 5, .read_timeout_sec = 2});
  auto [status, body] = client.get("/v1/json");
  EXPECT_EQ(status, 200);
  EXPECT_EQ(body.value("key", ""), "value");
}

}  // namespace charybdis
