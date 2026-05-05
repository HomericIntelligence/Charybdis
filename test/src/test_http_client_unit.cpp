/**
 * @file test_http_client_unit.cpp
 * @brief Unit tests for HttpTestClient — no live server required.
 *
 * Covers URL parsing logic in the constructor, the connection-failure
 * branches (status == 0 returns) in get/post/del/post_raw/is_healthy,
 * and the successful-response body-parsing paths via an in-process mock
 * httplib server.
 */

#include "projectcharybdis/http_test_client.hpp"

#include <httplib.h>
#include <thread>

#include <gtest/gtest.h>

namespace projectcharybdis {

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

// ── Port validation ───────────────────────────────────────────────────────────

TEST(HttpTestClientPortValidation, PortAtLowerBound) {
  // Port 0 is valid (OS assigns ephemeral port on bind); no throw on construction.
  EXPECT_NO_THROW({ HttpTestClient client("http://127.0.0.1:0"); });
}

TEST(HttpTestClientPortValidation, PortAtUpperBound) {
  EXPECT_NO_THROW({ HttpTestClient client("http://127.0.0.1:65535"); });
}

TEST(HttpTestClientPortValidation, PortJustAboveUpperBound) {
  EXPECT_THROW({ HttpTestClient client("http://127.0.0.1:65536"); }, std::invalid_argument);
}

TEST(HttpTestClientPortValidation, PortFarOutOfRange) {
  EXPECT_THROW({ HttpTestClient client("http://127.0.0.1:99999"); }, std::invalid_argument);
}

TEST(HttpTestClientPortValidation, PortOverflowsInt) {
  EXPECT_THROW({ HttpTestClient client("http://127.0.0.1:2200000000"); }, std::invalid_argument);
}

// ── Connection-failure paths (port 1 is always refused) ───────────────────────

class HttpTestClientOffline : public ::testing::Test {
 protected:
  // Port 1 is privileged and always connection-refused on Linux runners.
  HttpTestClientOffline() : client_("http://127.0.0.1:1") {}
  HttpTestClient client_;
};

TEST_F(HttpTestClientOffline, GetReturnsZeroStatus) {
  auto [status, body] = client_.get("/any/path");
  EXPECT_EQ(status, 0);
  EXPECT_TRUE(body.empty());
}

TEST_F(HttpTestClientOffline, PostReturnsZeroStatus) {
  nlohmann::json payload = {{"key", "value"}};
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
      std::string big(HttpTestClient::kMaxBodyBytes + 1, 'x');
      res.set_content(big, "text/plain");
    });

    svr_.Post("/v1/oversized-echo", [](const httplib::Request& /*req*/, httplib::Response& res) {
      std::string big(HttpTestClient::kMaxBodyBytes + 1, 'x');
      res.set_content(big, "text/plain");
    });

    svr_.Delete("/v1/oversized", [](const httplib::Request& /*req*/, httplib::Response& res) {
      std::string big(HttpTestClient::kMaxBodyBytes + 1, 'x');
      res.set_content(big, "text/plain");
    });

    svr_.Get("/v1/boundary", [](const httplib::Request& /*req*/, httplib::Response& res) {
      // Exactly at the limit — not rejected
      std::string exact(HttpTestClient::kMaxBodyBytes, 'x');
      res.set_content(exact, "text/plain");
    });

    thread_ = std::thread([this]() { svr_.listen_after_bind(); });

    // Wait until the server is actually accepting connections
    while (!svr_.is_running()) {
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
  }

  ~MockServer() {
    svr_.stop();
    if (thread_.joinable()) thread_.join();
  }

  int port() const { return port_; }

 private:
  httplib::Server svr_;
  int port_{0};
  std::thread thread_;
};

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
  nlohmann::json payload = {{"ping", "pong"}};
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

}  // namespace projectcharybdis
