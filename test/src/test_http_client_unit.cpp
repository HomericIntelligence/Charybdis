/**
 * @file test_http_client_unit.cpp
 * @brief Unit tests for HttpTestClient — no live server required.
 *
 * Covers URL parsing logic in the constructor, the connection-failure
 * branches (status == 0 returns) in get/post/del/post_raw/is_healthy,
 * and the successful-response body-parsing paths via an in-process mock
 * httplib server.
 */

#include "charybdis/http_test_client.hpp"

#include <chrono>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <thread>

#include <gtest/gtest.h>

namespace charybdis {

// ── Constructor / URL parsing ─────────────────────────────────────────────────

TEST(HttpTestClientUnit, ValidUrlParsingRefused) {
  // Port 1 gives immediate ECONNREFUSED — exercises the parsed-URL code path.
  HttpTestClient client("http://127.0.0.1:1");
  EXPECT_FALSE(client.is_healthy());
}

TEST(HttpTestClientUnit, MalformedUrlFallsBackToDefaults) {
  // A URL that doesn't match the regex exercises the else branch in the
  // constructor. Falls back to host=localhost, port=8080.
  // On a CI runner nothing listens on 8080, so is_healthy() returns false.
  HttpTestClient client("not-a-url");
  EXPECT_FALSE(client.is_healthy());
}

TEST(HttpTestClientUnit, HttpsUrlParsing) {
  // The regex also handles https://; port 1 gives immediate refusal.
  HttpTestClient client("https://127.0.0.1:1");
  EXPECT_FALSE(client.is_healthy());
}

TEST(HttpTestClientUnit, OutOfRangePortThrows) {
  // 11-digit value overflows int — stoi throws std::out_of_range, rethrown as std::runtime_error.
  EXPECT_THROW(HttpTestClient("http://127.0.0.1:99999999999"), std::runtime_error);
}

TEST(HttpTestClientUnit, OutOfValidPortRangeThrows) {
  // 99999 fits in int but exceeds the valid TCP port range [1,65535].
  EXPECT_THROW(HttpTestClient("http://127.0.0.1:99999"), std::runtime_error);
}

// ── Connection-failure paths (port 1 is always refused) ───────────────────────

// NOLINTNEXTLINE(misc-use-internal-linkage)
class HttpTestClientOffline : public ::testing::Test {
 protected:
  // Port 1 is privileged and always connection-refused on Linux runners.
  // NOLINTNEXTLINE(hicpp-use-equals-default,modernize-use-equals-default)
  HttpTestClientOffline() : client_("http://127.0.0.1:1") {}
  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
  HttpTestClient client_;
};

TEST_F(HttpTestClientOffline, GetReturnsZeroStatus) {
  auto [status, body] = client_.get("/any/path");
  EXPECT_EQ(status, 0);
  EXPECT_TRUE(body.empty());
}

TEST_F(HttpTestClientOffline, PostReturnsZeroStatus) {
  const nlohmann::json payload = {{"key", "value"}};
  auto [status, body] = client_.post("/any/path", payload);
  EXPECT_EQ(status, 0);
  EXPECT_TRUE(body.empty());
}

TEST_F(HttpTestClientOffline, PostEmptyBodyReturnsZeroStatus) {
  auto [status, body] = client_.post("/any/path");
  EXPECT_EQ(status, 0);
  EXPECT_TRUE(body.empty());
}

TEST_F(HttpTestClientOffline, DelReturnsZeroStatus) {
  auto [status, body] = client_.del("/any/path");
  EXPECT_EQ(status, 0);
  EXPECT_TRUE(body.empty());
}

TEST_F(HttpTestClientOffline, PostRawReturnsZeroStatus) {
  auto [status, body] = client_.post_raw("/any/path", "raw body", "text/plain");
  EXPECT_EQ(status, 0);
  EXPECT_TRUE(body.empty());
}

TEST_F(HttpTestClientOffline, IsHealthyReturnsFalse) { EXPECT_FALSE(client_.is_healthy()); }

// ── Mock server: exercises the response-body parsing paths ────────────────────

/// Minimal in-process HTTP mock server for unit tests.
// NOLINTNEXTLINE(misc-use-internal-linkage)
class MockServer {
 public:
  explicit MockServer() {
    // Bind to an OS-assigned free port
    port_ = svr_.bind_to_any_port("127.0.0.1");

    svr_.Get("/v1/health", [](const httplib::Request& /*req*/, httplib::Response& res) {
      res.set_content(R"({"status":"ok"})", "application/json");
    });

    svr_.Get("/v1/json", [](const httplib::Request& /*req*/, httplib::Response& res) {
      res.set_content(R"({"key":"value"})", "application/json");
    });

    svr_.Get("/v1/text", [](const httplib::Request& /*req*/, httplib::Response& res) {
      res.set_content("not-json", "text/plain");
    });

    svr_.Post("/v1/echo", [](const httplib::Request& req, httplib::Response& res) {
      res.set_content(req.body, "application/json");
    });

    svr_.Delete("/v1/item", [](const httplib::Request& /*req*/, httplib::Response& res) {
      res.set_content(R"({"deleted":true})", "application/json");
    });

    svr_.Get("/v1/oversized", [](const httplib::Request& /*req*/, httplib::Response& res) {
      const std::string big(HttpTestClient::kMaxBodyBytes + 1, 'x');
      res.set_content(big, "text/plain");
    });

    svr_.Post("/v1/oversized-echo", [](const httplib::Request& /*req*/, httplib::Response& res) {
      const std::string big(HttpTestClient::kMaxBodyBytes + 1, 'x');
      res.set_content(big, "text/plain");
    });

    svr_.Delete("/v1/oversized", [](const httplib::Request& /*req*/, httplib::Response& res) {
      const std::string big(HttpTestClient::kMaxBodyBytes + 1, 'x');
      res.set_content(big, "text/plain");
    });

    svr_.Get("/v1/boundary", [](const httplib::Request& /*req*/, httplib::Response& res) {
      // Exactly at the limit — not rejected
      const std::string exact(HttpTestClient::kMaxBodyBytes, 'x');
      res.set_content(exact, "text/plain");
    });

    thread_ = std::thread([this]() { svr_.listen_after_bind(); });

    // Wait until the server is actually accepting connections (5 s timeout)
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!svr_.is_running()) {
      if (std::chrono::steady_clock::now() > deadline) {
        throw std::runtime_error("MockServer failed to start within 5 seconds");
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
  }

  ~MockServer() {
    svr_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  MockServer(const MockServer&) = delete;
  MockServer& operator=(const MockServer&) = delete;
  MockServer(MockServer&&) = delete;
  MockServer& operator=(MockServer&&) = delete;

  [[nodiscard]] int port() const { return port_; }

 private:
  httplib::Server svr_;
  int port_{0};
  std::thread thread_;
};

// NOLINTNEXTLINE(misc-use-internal-linkage)
class HttpTestClientOnline : public ::testing::Test {
 protected:
  void SetUp() override {
    mock_ = std::make_unique<MockServer>();
    client_ = std::make_unique<HttpTestClient>("http://127.0.0.1:" + std::to_string(mock_->port()));
  }

  void TearDown() override {
    client_.reset();
    mock_.reset();
  }

  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
  std::unique_ptr<MockServer> mock_;
  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
  std::unique_ptr<HttpTestClient> client_;
};

TEST_F(HttpTestClientOnline, IsHealthyReturnsTrueWhenServerOk) {
  EXPECT_TRUE(client_->is_healthy());
}

TEST_F(HttpTestClientOnline, GetParsesJsonBody) {
  auto [status, body] = client_->get("/v1/json");
  EXPECT_EQ(status, 200);
  EXPECT_EQ(body.value("key", ""), "value");
}

TEST_F(HttpTestClientOnline, GetHandlesNonJsonBody) {
  // The catch block wraps non-JSON body in {"raw": ...}
  auto [status, body] = client_->get("/v1/text");
  EXPECT_EQ(status, 200);
  EXPECT_TRUE(body.contains("raw"));
}

TEST_F(HttpTestClientOnline, PostSendsJsonBody) {
  const nlohmann::json payload = {{"ping", "pong"}};
  auto [status, body] = client_->post("/v1/echo", payload);
  EXPECT_EQ(status, 200);
}

TEST_F(HttpTestClientOnline, PostRawSendsStringBody) {
  auto [status, body] = client_->post_raw("/v1/echo", R"({"raw":true})", "application/json");
  EXPECT_EQ(status, 200);
}

TEST_F(HttpTestClientOnline, DelParsesJsonResponse) {
  auto [status, body] = client_->del("/v1/item");
  EXPECT_EQ(status, 200);
  EXPECT_TRUE(body.value("deleted", false));
}

TEST_F(HttpTestClientOnline, GetRejectsOversizedBody) {
  auto [status, body] = client_->get("/v1/oversized");
  EXPECT_EQ(body.value("error", ""), "response_too_large");
}

TEST_F(HttpTestClientOnline, PostRejectsOversizedBody) {
  auto [status, body] = client_->post("/v1/oversized-echo", {{"x", 1}});
  EXPECT_EQ(body.value("error", ""), "response_too_large");
}

TEST_F(HttpTestClientOnline, PostRawRejectsOversizedBody) {
  auto [status, body] = client_->post_raw("/v1/oversized-echo", R"({"x":1})", "application/json");
  EXPECT_EQ(body.value("error", ""), "response_too_large");
}

TEST_F(HttpTestClientOnline, DelRejectsOversizedBody) {
  auto [status, body] = client_->del("/v1/oversized");
  EXPECT_EQ(body.value("error", ""), "response_too_large");
}

TEST_F(HttpTestClientOnline, BoundaryBodyNotRejected) {
  // Exactly kMaxBodyBytes — must not trigger the size guard
  auto [status, body] = client_->get("/v1/boundary");
  EXPECT_EQ(status, 200);
  EXPECT_FALSE(body.value("error", "").find("response_too_large") != std::string::npos);
}

// ── client_ single-construction (#82) ─────────────────────────────────────────
//
// Policy (feedback_http_client_lifetime_object.md): connection-holding wrappers
// must hold the connection as a member for the object's lifetime, not construct
// per-call. We assert this directly: the address of the underlying
// `httplib::Client` returned by `test_client_ptr()` (a test-only accessor added
// for #82) must remain stable across get/post/del/is_healthy calls. If a
// regression made `HttpTestClient` reconstruct `client_` on each call, the
// pointer would change and these tests would fail.

TEST_F(HttpTestClientOnline, ClientPointerStableAcrossGetPostDel) {
  // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
  const httplib::Client* before = client_->test_client_ptr();
  ASSERT_NE(before, nullptr);

  auto [gs, gb] = client_->get("/v1/json");
  EXPECT_EQ(gs, 200);
  EXPECT_EQ(client_->test_client_ptr(), before);

  auto [ps, pb] = client_->post("/v1/echo", {{"k", "v"}});
  EXPECT_EQ(ps, 200);
  EXPECT_EQ(client_->test_client_ptr(), before);

  auto [ds, db] = client_->del("/v1/item");
  EXPECT_EQ(ds, 200);
  EXPECT_EQ(client_->test_client_ptr(), before);
}

namespace {

// Helper extracted to keep cognitive-complexity below the clang-tidy threshold
// in the multi-round single-construction test.
void run_round_and_assert_stable(HttpTestClient& client, const httplib::Client* baseline,
                                 int round) {
  EXPECT_EQ(client.get("/v1/json").status, 200);
  EXPECT_EQ(client.post("/v1/echo", {{"i", round}}).status, 200);
  EXPECT_EQ(client.del("/v1/item").status, 200);
  EXPECT_EQ(client.test_client_ptr(), baseline);
}

}  // namespace

TEST_F(HttpTestClientOnline, ClientPointerStableAcrossManyCalls) {
  // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
  const httplib::Client* before = client_->test_client_ptr();
  ASSERT_NE(before, nullptr);

  constexpr int kRounds = 5;
  for (int round = 0; round < kRounds; ++round) {
    run_round_and_assert_stable(*client_, before, round);
  }
}

TEST_F(HttpTestClientOnline, ClientPointerStableAcrossIsHealthyAndCalls) {
  // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
  const httplib::Client* before = client_->test_client_ptr();
  ASSERT_NE(before, nullptr);

  // is_healthy() internally calls get("/v1/health") — same underlying client_.
  EXPECT_TRUE(client_->is_healthy());
  EXPECT_EQ(client_->test_client_ptr(), before);

  EXPECT_EQ(client_->get("/v1/json").status, 200);
  EXPECT_EQ(client_->test_client_ptr(), before);

  EXPECT_EQ(client_->post("/v1/echo", {{"k", "v"}}).status, 200);
  EXPECT_EQ(client_->test_client_ptr(), before);
}

}  // namespace charybdis
