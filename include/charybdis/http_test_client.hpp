#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace httplib {
class Client;
}  // namespace httplib

namespace charybdis {

/// Retry policy for transient connection failures (status == 0).
/// Default is `max_retries = 0` — retries are opt-in to preserve the existing
/// fail-fast behaviour for tests that intentionally probe an offline service.
/// 4xx/5xx responses are deliberate replies and are never retried.
struct RetryPolicy {
  int max_retries = 0;  // additional attempts after first (total = max_retries + 1)
  int base_delay_ms = 100;
  int max_delay_ms = 2000;
  double backoff_mult = 2.0;  // exponential factor; jitter is uniform(0.5, 1.5)
};

/// Configuration for the per-client circuit breaker.
/// Default `failure_threshold = 0` disables the breaker entirely — opt-in to
/// preserve existing call semantics. When enabled, consecutive transient
/// failures (status == 0) trip the breaker; while OPEN, calls short-circuit to
/// `{0, {}}` without touching the network until `open_duration_ms` elapses,
/// after which a single HALF_OPEN probe is allowed.
struct CircuitBreakerConfig {
  int failure_threshold = 0;  // 0 disables the breaker
  int open_duration_ms = 10000;
  int success_threshold = 2;  // consecutive HALF_OPEN successes required to close
};

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

  /// Construct with optional retry and circuit-breaker policies. The defaults
  /// preserve pre-#39 behaviour (no retries, no breaker), so existing callers
  /// require no source changes.
  explicit HttpTestClient(const std::string& base_url = "http://localhost:8080",
                          RetryPolicy retry = {}, CircuitBreakerConfig breaker_cfg = {});
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

  /// Test-only accessor: returns a stable pointer to the underlying
  /// `httplib::Client`. Used by the single-construction unit test (issue #82)
  /// to assert that `client_` is created exactly once and reused for the
  /// lifetime of this object — not reconstructed on every get/post/del call.
  /// The pointer must not be used to mutate `client_`'s ownership.
  [[nodiscard]] const httplib::Client* test_client_ptr() const { return client_.get(); }

  /// Test-only circuit-breaker state. CLOSED is the normal pass-through state.
  enum class BreakerState : std::uint8_t { kClosed, kOpen, kHalfOpen };

  /// Test-only accessor — returns the current breaker state. Defined for unit
  /// tests; production callers should not depend on this.
  [[nodiscard]] BreakerState test_breaker_state() const;

 private:
  static constexpr int kConnectionTimeoutSec = 5;
  static constexpr int kReadTimeoutSec = 10;

  std::string host_;
  int port_;
  std::unique_ptr<httplib::Client> client_;

  RetryPolicy retry_;
  // CircuitBreaker is defined in the .cpp; held via unique_ptr to keep the
  // header free of <atomic>/<mutex>/<random> and preserve ABI flexibility.
  struct CircuitBreaker;
  std::unique_ptr<CircuitBreaker> cb_;

  /// Internal: apply the retry-with-backoff envelope around an httplib call.
  /// `func` must be invocable and return an `httplib::Result`-like value
  /// (truthy on response, falsy on transient failure). Implemented as a
  /// private static so it can refer to the private `CircuitBreaker` type.
  template <typename Fn>
  static Response run_with_retry(const RetryPolicy& policy, CircuitBreaker& breaker, Fn func);
};

}  // namespace charybdis
