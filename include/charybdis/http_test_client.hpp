#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace httplib {
class Client;
struct Response;
}  // namespace httplib

namespace charybdis {

/// Per-call socket timeouts for the underlying httplib::Client.
/// Defaults reproduce pre-#81 behaviour (5s connect, 10s read). Override
/// `read_timeout_sec` to a larger value in fixtures that drive
/// `POST /v1/chaos/latency`, where the injected delay would otherwise be
/// indistinguishable from a real read-timeout failure.
///
/// Values must be >= 0; negative values are rejected by the HttpTestClient
/// constructor. Note on zero: cpp-httplib 0.18.x forwards these values to a
/// select()/poll() wait, so sec=0 means a *zero-length* wait — requests fail
/// almost immediately unless a response is already available. It does NOT
/// disable the timeout. To tolerate slow (latency-injected) endpoints, use a
/// large positive value instead.
struct TimeoutConfig {
  int connection_timeout_sec = 5;
  int read_timeout_sec = 10;
};

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

/// Retry policy for online/chaos-injection tests where the target server may
/// be killed and restarted between or during requests. Sized to cover a
/// `systemd Restart=on-failure RestartSec=1s` supervisor window: three
/// retries at 400/800/1600 ms nominal ≈ 2.8 s total; even at the jitter
/// floor uniform(0.5, 1.5) the minimum total is 0.5*(400+800+1600) = 1400 ms,
/// comfortably ≥ the 1 s window with margin. Inline constexpr so
/// every TU including this header sees the same definition (C++17 ODR-safe).
inline constexpr RetryPolicy kChaosResilientPolicy{
    /*max_retries=*/3,
    /*base_delay_ms=*/400,
    /*max_delay_ms=*/2000,
    /*backoff_mult=*/2.0,
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
///
/// **Reconnect contract (issue #80):** This client deliberately uses
/// httplib's default `keep_alive_=false` (verified in cpp-httplib 0.18.3
/// at httplib.h:1540). Every request opens a fresh TCP connection: see
/// `ClientImpl::send_` at httplib.h:7494 where `close_connection` is set
/// from `!keep_alive_`, and the scope_exit at httplib.h:7505-7509 closes
/// the socket unconditionally on that branch. The persistent `client_`
/// member is therefore immune to a stale-connection failure after a
/// chaos server is killed and restarted. The remaining failure mode —
/// a SIGKILL landing during connect or write — is handled by passing
/// `kChaosResilientPolicy` for the `retry` parameter in chaos fixtures.
/// Do NOT call `set_keep_alive(true)` on this client: that would
/// reintroduce the stale-socket race the per-call-reconnect default
/// avoids.
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions,hicpp-special-member-functions)
class HttpTestClient {
 public:
  /// Maximum response body size accepted by the client. Responses exceeding this
  /// limit are rejected and `Response::body` is replaced with
  /// `{"error": "response_too_large"}` (status code is preserved). Callers asserting
  /// on response shape must account for this contract.
  // NOLINTNEXTLINE(bugprone-implicit-widening-of-multiplication-result)
  static constexpr std::size_t kMaxBodyBytes = 10 * 1024 * 1024;  // 10 MB

  /// Construct a client targeting `base_url` with optional retry,
  /// circuit-breaker and timeout policies. The defaults preserve
  /// pre-#39/#81 behaviour (no retries, no breaker, 5s connect / 10s read
  /// timeouts), so existing callers require no source changes.
  ///
  /// URL handling: a `base_url` that does not match `https?://host:port`
  /// silently falls back to `localhost:8080`. A URL that *does* match but
  /// carries an invalid port fails construction.
  ///
  /// Callers constructing from environment-derived URLs (e.g. `agamemnon_url()`)
  /// should treat construction as failable or rely on the default-URL fallback.
  ///
  /// @param base_url Base URL of the target service (default
  ///   `"http://localhost:8080"`).
  /// @param retry Retry policy for transient failures (default: disabled).
  /// @param breaker_cfg Circuit-breaker configuration (default: disabled).
  /// @param timeouts Socket timeouts (default: 5s connect / 10s read).
  /// @throws std::runtime_error if `base_url` matches `https?://host:port` but
  ///   the port is non-numeric, overflows `int`, or lies outside `[1, 65535]`,
  ///   or if any timeout value is negative. No other exceptions escape
  ///   construction under normal operation; `std::bad_alloc` may propagate from
  ///   allocation as usual.
  explicit HttpTestClient(const std::string& base_url = "http://localhost:8080",
                          RetryPolicy retry = {}, CircuitBreakerConfig breaker_cfg = {},
                          TimeoutConfig timeouts = {});
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
  std::string host_;
  int port_;
  std::unique_ptr<httplib::Client> client_;

  RetryPolicy retry_;
  TimeoutConfig timeouts_;
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

  /// Internal: single home for response-body handling (issue #142) — size guard
  /// against `kMaxBodyBytes`, then JSON parse, wrapping non-JSON bodies as
  /// `{"raw": <body>}`. Takes `httplib::Response` (not `httplib::Result`):
  /// null results are the retry loop's transient-failure signal and never
  /// reach parsing. Future changes (raising the cap, adding metrics) go here.
  /// Spelled `httplib::Response` because this class nests its own `Response`.
  static nlohmann::json parse_response(const httplib::Response& res);
};

}  // namespace charybdis
