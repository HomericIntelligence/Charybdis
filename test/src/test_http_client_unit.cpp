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

#include <atomic>
#include <chrono>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
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

TEST(HttpTestClientUnit, InvalidPortErrorMessageContainsPort) {
  // Documented contract (issue #194): the diagnostic message embeds the
  // offending port text so failures are actionable.
  try {
    const HttpTestClient client("http://127.0.0.1:70000");
    (void)client;
    FAIL() << "Expected std::runtime_error for out-of-range port";
  } catch (const std::runtime_error& e) {
    EXPECT_NE(std::string(e.what()).find("70000"), std::string::npos);
  }
}

// ── Connection-failure paths (port 1 is always refused) ───────────────────────

// NOLINTNEXTLINE(misc-use-internal-linkage)
class HttpTestClientOffline : public ::testing::Test {
 protected:
  // Port 1 is privileged and always connection-refused on Linux runners.
  // NOLINTNEXTLINE(hicpp-use-equals-default,modernize-use-equals-default)
  HttpTestClientOffline() : client_("http://127.0.0.1:1") {}
  HttpTestClient client_;
};

TEST_F(HttpTestClientOffline, GetReturnsZeroStatus) {
  auto [status, body] = client_.get("/any/path");
  (void)status;
  EXPECT_EQ(status, 0);
  EXPECT_TRUE(body.empty());
}

TEST_F(HttpTestClientOffline, PostReturnsZeroStatus) {
  const nlohmann::json payload = {{"key", "value"}};
  auto [status, body] = client_.post("/any/path", payload);
  (void)status;
  EXPECT_EQ(status, 0);
  EXPECT_TRUE(body.empty());
}

TEST_F(HttpTestClientOffline, PostEmptyBodyReturnsZeroStatus) {
  auto [status, body] = client_.post("/any/path");
  (void)status;
  EXPECT_EQ(status, 0);
  EXPECT_TRUE(body.empty());
}

TEST_F(HttpTestClientOffline, DelReturnsZeroStatus) {
  auto [status, body] = client_.del("/any/path");
  (void)status;
  EXPECT_EQ(status, 0);
  EXPECT_TRUE(body.empty());
}

TEST_F(HttpTestClientOffline, PostRawReturnsZeroStatus) {
  auto [status, body] = client_.post_raw("/any/path", "raw body", "text/plain");
  (void)status;
  EXPECT_EQ(status, 0);
  EXPECT_TRUE(body.empty());
}

TEST_F(HttpTestClientOffline, IsHealthyReturnsFalse) { EXPECT_FALSE(client_.is_healthy()); }

// ── Mock server: exercises the response-body parsing paths ────────────────────

/// Minimal in-process HTTP mock server for unit tests.
// NOLINTNEXTLINE(misc-use-internal-linkage)
class MockServer {
 public:
  // OS-assigned ephemeral port (unchanged behaviour for existing tests).
  MockServer() {
    port_ = svr_.bind_to_any_port("127.0.0.1");
    if (port_ <= 0) {
      throw std::runtime_error("MockServer: bind_to_any_port failed");
    }
    register_routes();
    start_thread_and_wait();
  }

  // Fixed-port ctor used by HttpTestClientReconnect to rebind the same
  // port after a stop. Verified against the cpp-httplib 0.18.3 header
  // pinned in conan.lock: declaration at httplib.h:1004, definition at
  // httplib.h:6310 — `bool bind_to_port(const std::string&, int, int=0)`.
  explicit MockServer(int fixed_port) {
    if (!svr_.bind_to_port("127.0.0.1", fixed_port)) {
      throw std::runtime_error("MockServer: bind_to_port failed on " + std::to_string(fixed_port));
    }
    port_ = fixed_port;
    register_routes();
    start_thread_and_wait();
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
  void register_routes() {
    // KB cpp-httplib-lambda-capture-ub: capture by value only. cpp-httplib
    // stores route lambdas as std::function copies that outlive this
    // method's stack frame, so [&] captures of locals would dangle.
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

    // Issue #80 abrupt-disconnect support: the first hit announces a body of
    // 1024 bytes, writes a few, then aborts the content provider. cpp-httplib
    // treats a provider returning false as a fatal stream error and closes
    // the TCP connection mid-response — the same transport-level abrupt close
    // a SIGKILLed server produces, but fully deterministic (no cross-thread
    // teardown race; note svr_.stop() waits for in-flight handlers, so
    // destroying the server cannot cut off an in-flight request).
    // Subsequent hits return normally, so retry policies can recover.
    // The handler captures `this` by value and only reads `flaky_hits_`
    // atomically — it never mutates `svr_` (no reentrant stop call).
    svr_.Get("/v1/flaky-once", [this](const httplib::Request& /*req*/, httplib::Response& res) {
      if (flaky_hits_.fetch_add(1) == 0) {
        res.set_content_provider(
            1024, "application/json",
            [](size_t /*offset*/, size_t /*length*/, httplib::DataSink& sink) -> bool {
              static constexpr std::string_view kPartial = R"({"partial":)";
              sink.write(kPartial.data(), kPartial.size());
              return false;  // abort → abrupt TCP close mid-body
            });
      } else {
        res.set_content(R"({"flaky":"ok"})", "application/json");
      }
    });
  }

  void start_thread_and_wait() {
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

  httplib::Server svr_;
  int port_{0};
  std::thread thread_;
  std::atomic<int> flaky_hits_{0};
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

  std::unique_ptr<MockServer> mock_;
  std::unique_ptr<HttpTestClient> client_;
};

TEST_F(HttpTestClientOnline, IsHealthyReturnsTrueWhenServerOk) {
  EXPECT_TRUE(client_->is_healthy());
}

TEST_F(HttpTestClientOnline, GetParsesJsonBody) {
  auto [status, body] = client_->get("/v1/json");
  (void)status;
  EXPECT_EQ(status, 200);
  EXPECT_EQ(body.value("key", ""), "value");
}

TEST_F(HttpTestClientOnline, GetHandlesNonJsonBody) {
  // The catch block wraps non-JSON body in {"raw": ...}
  auto [status, body] = client_->get("/v1/text");
  (void)status;
  EXPECT_EQ(status, 200);
  EXPECT_TRUE(body.contains("raw"));
}

TEST_F(HttpTestClientOnline, PostSendsJsonBody) {
  const nlohmann::json payload = {{"ping", "pong"}};
  auto [status, body] = client_->post("/v1/echo", payload);
  (void)status;
  EXPECT_EQ(status, 200);
}

TEST_F(HttpTestClientOnline, PostRawSendsStringBody) {
  auto [status, body] = client_->post_raw("/v1/echo", R"({"raw":true})", "application/json");
  (void)status;
  EXPECT_EQ(status, 200);
}

TEST_F(HttpTestClientOnline, DelParsesJsonResponse) {
  auto [status, body] = client_->del("/v1/item");
  (void)status;
  EXPECT_EQ(status, 200);
  EXPECT_TRUE(body.value("deleted", false));
}

TEST_F(HttpTestClientOnline, GetRejectsOversizedBody) {
  const auto resp = client_->get("/v1/oversized");
  EXPECT_EQ(resp.body.value("error", ""), "response_too_large");
  EXPECT_NE(resp.status, 0);
}

TEST_F(HttpTestClientOnline, PostRejectsOversizedBody) {
  const auto resp = client_->post("/v1/oversized-echo", {{"x", 1}});
  EXPECT_EQ(resp.body.value("error", ""), "response_too_large");
  EXPECT_NE(resp.status, 0);
}

TEST_F(HttpTestClientOnline, PostRawRejectsOversizedBody) {
  const auto resp =
      client_->post_raw("/v1/oversized-echo", R"({"x":1})", "application/json");
  EXPECT_EQ(resp.body.value("error", ""), "response_too_large");
  EXPECT_NE(resp.status, 0);
}

TEST_F(HttpTestClientOnline, DelRejectsOversizedBody) {
  const auto resp = client_->del("/v1/oversized");
  EXPECT_EQ(resp.body.value("error", ""), "response_too_large");
  EXPECT_NE(resp.status, 0);
}

TEST_F(HttpTestClientOnline, BoundaryBodyNotRejected) {
  // Exactly kMaxBodyBytes — must not trigger the size guard
  auto [status, body] = client_->get("/v1/boundary");
  (void)status;
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

  EXPECT_EQ(client_->get("/v1/json").status, 200);
  EXPECT_EQ(client_->test_client_ptr(), before);

  EXPECT_EQ(client_->post("/v1/echo", {{"k", "v"}}).status, 200);
  EXPECT_EQ(client_->test_client_ptr(), before);

  EXPECT_EQ(client_->del("/v1/item").status, 200);
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

// ── Server-restart reconnect (issue #80) ─────────────────────────────────────
//
// Verified-from-source contract (cpp-httplib 0.18.3):
//   ClientImpl::send_ at httplib.h:7427 reads keep_alive_ (default false at
//   httplib.h:1540) into close_connection at httplib.h:7494, and the
//   scope_exit at httplib.h:7505-7509 closes the socket after every request.
//   The persistent HttpTestClient::client_ therefore opens a fresh TCP
//   connection per call. The tests below pin:
//   (a) graceful restart: client survives stop+rebind on the same port
//   (b) is_healthy after restart (the chaos-test idiom)
//   (c) abrupt mid-flight transport failure WITH kChaosResilientPolicy
//       recovers (200 — retry lands on the healthy server)
//   (d) abrupt mid-flight transport failure WITHOUT retries fails fast (0)

class HttpTestClientReconnect : public ::testing::Test {
 protected:
  void SetUp() override {
    mock_ = std::make_unique<MockServer>();
    port_ = mock_->port();
  }

  // Destroy and rebind the same port. TIME_WAIT may briefly hold it.
  void restart_server_on_same_port() {
    mock_.reset();
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (std::chrono::steady_clock::now() < deadline) {
      try {
        mock_ = std::make_unique<MockServer>(port_);
        return;
        // NOLINTNEXTLINE(hicpp-avoid-c-arrays,bugprone-exception-escape)
      } catch (const std::runtime_error&) {
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
      }
    }
    FAIL() << "Failed to rebind MockServer on port " << port_;
  }

  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
  std::unique_ptr<MockServer> mock_;
  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
  int port_{0};
};

// (a) Graceful restart: a single persistent HttpTestClient survives a full
// stop+rebind cycle without `client_` being reconstructed.
TEST_F(HttpTestClientReconnect, ReconnectsAfterGracefulServerRestart) {
  HttpTestClient client("http://127.0.0.1:" + std::to_string(port_), kChaosResilientPolicy);
  ASSERT_EQ(client.get("/v1/json").status, 200);
  const httplib::Client* before = client.test_client_ptr();
  ASSERT_NE(before, nullptr);

  restart_server_on_same_port();

  EXPECT_EQ(client.get("/v1/json").status, 200);
  EXPECT_EQ(client.test_client_ptr(), before)
      << "client_ must not be reconstructed; httplib reconnects per request";
}

// (b) is_healthy() — the call chaos tests use after a kill fault — survives
// a graceful restart.
TEST_F(HttpTestClientReconnect, IsHealthySurvivesServerRestart) {
  HttpTestClient client("http://127.0.0.1:" + std::to_string(port_), kChaosResilientPolicy);
  ASSERT_TRUE(client.is_healthy());
  restart_server_on_same_port();
  EXPECT_TRUE(client.is_healthy());
}

// (c) THE CORE issue #80 SCENARIO. With kChaosResilientPolicy, a transport-
// level abrupt close mid-response (the /v1/flaky-once provider abort) MUST
// be retried, and the retry MUST land on the healthy server → status 200.
// Falsifiable: delete the retry-with-backoff envelope from
// run_with_retry and this test fails (single attempt → status 0).
TEST_F(HttpTestClientReconnect, RetryPolicyRecoversFromMidFlightTeardown) {
  HttpTestClient client("http://127.0.0.1:" + std::to_string(port_), kChaosResilientPolicy);

  EXPECT_EQ(client.get("/v1/flaky-once").status, 200)
      << "kChaosResilientPolicy must retry past an abrupt mid-flight "
         "disconnect and succeed on the next attempt";
}

// (d) Counter-evidence: WITHOUT retries, the same abrupt mid-flight
// disconnect MUST return status 0 — there is no retry budget. Falsifiable:
// if the default policy silently retried, this test would return 200.
TEST_F(HttpTestClientReconnect, DefaultPolicyFailsFastOnMidFlightTeardown) {
  HttpTestClient strict("http://127.0.0.1:" + std::to_string(port_));

  EXPECT_EQ(strict.get("/v1/flaky-once").status, 0)
      << "default policy has no retry budget; mid-flight teardown must "
         "surface as transport error (status 0)";
}

}  // namespace charybdis
