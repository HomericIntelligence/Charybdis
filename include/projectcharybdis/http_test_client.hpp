#pragma once

#include <memory>
#include <nlohmann/json.hpp>
#include <string>

namespace httplib {
class Client;
}  // namespace httplib

namespace projectcharybdis {

/// Thin HTTP client for chaos/resilience GTest tests.
/// Wraps cpp-httplib for REST API interactions with Agamemnon.
///
/// **Thread-safety:** Not thread-safe. The underlying `httplib::Client` is held as a
/// member and reused across `get`/`post`/`del`/`post_raw` calls; `cpp-httplib`'s
/// `Client` does not support concurrent requests on the same instance. Each test
/// thread (or async task) must construct its own `HttpTestClient`. See issue #79.
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions,hicpp-special-member-functions)
class HttpTestClient {
 public:
  /// Maximum response body size accepted by the client. Responses exceeding this
  /// limit are rejected and `Response::body` is replaced with
  /// `{"error": "response_too_large"}` (status code is preserved). Callers asserting
  /// on response shape must account for this contract.
  // NOLINTNEXTLINE(bugprone-implicit-widening-of-multiplication-result)
  static constexpr std::size_t kMaxBodyBytes = 10 * 1024 * 1024;  // 10 MB

  explicit HttpTestClient(const std::string& base_url = "http://localhost:8080");
  ~HttpTestClient();

  /// HTTP response. `body` is `{"error": "response_too_large"}` if the raw response
  /// exceeded `kMaxBodyBytes`; the `status` field still reflects the server's reply.
  struct Response {
    int status;
    nlohmann::json body;
  };

  [[nodiscard]] Response get(const std::string& path);
  [[nodiscard]] Response post(const std::string& path, const nlohmann::json& body = {});
  [[nodiscard]] Response del(const std::string& path);

  /// POST with raw string body (for malformed payload tests). Subject to the same
  /// `kMaxBodyBytes` response-size cap as the other methods.
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  [[nodiscard]] Response post_raw(const std::string& path, const std::string& body,
                                  const std::string& content_type = "application/json");

  [[nodiscard]] bool is_healthy();

 private:
  static constexpr int kConnectionTimeoutSec = 5;
  static constexpr int kReadTimeoutSec = 10;

  std::string host_;
  int port_;
  std::unique_ptr<httplib::Client> client_;
};

}  // namespace projectcharybdis
