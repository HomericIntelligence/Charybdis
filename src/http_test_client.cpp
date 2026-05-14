#include "projectcharybdis/http_test_client.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <httplib.h>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <random>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>

namespace projectcharybdis {

namespace {

nlohmann::json parse_body(const httplib::Response& res) {
  if (res.body.size() > HttpTestClient::kMaxBodyBytes) {
    return {{"error", "response_too_large"}};
  }
  try {
    return nlohmann::json::parse(res.body);
  } catch (...) {
    return {{"raw", res.body}};
  }
}

/// Thread-local jitter source. Avoids per-call construction of a Mersenne
/// twister and avoids a global mutex on the RNG.
double jitter_factor() {
  static thread_local std::mt19937 rng{std::random_device{}()};
  std::uniform_real_distribution<double> dist(0.5, 1.5);
  return dist(rng);
}

}  // namespace

// ── CircuitBreaker (private, header-free) ────────────────────────────────────
//
// Three-state breaker: CLOSED → OPEN (after `failure_threshold` consecutive
// failures) → HALF_OPEN (after `open_duration_ms` has elapsed since opening).
// In HALF_OPEN a single probe is permitted; on success we count it and close
// after `success_threshold` consecutive HALF_OPEN successes, on failure we
// re-open. With `failure_threshold == 0` the breaker is disabled and every
// call is allowed (matching pre-#39 behaviour).
struct HttpTestClient::CircuitBreaker {
  explicit CircuitBreaker(CircuitBreakerConfig cfg) : cfg_(cfg) {}

  bool allow_call() {
    if (cfg_.failure_threshold <= 0) {
      return true;  // disabled
    }
    const std::lock_guard<std::mutex> lock(mu_);
    if (state_ == BreakerState::kOpen) {
      const auto now = std::chrono::steady_clock::now();
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(now - opened_at_).count();
      if (elapsed >= cfg_.open_duration_ms) {
        state_ = BreakerState::kHalfOpen;
        half_open_successes_ = 0;
        return true;  // single probe
      }
      return false;
    }
    return true;
  }

  void record_success() {
    if (cfg_.failure_threshold <= 0) {
      return;
    }
    const std::lock_guard<std::mutex> lock(mu_);
    if (state_ == BreakerState::kHalfOpen) {
      ++half_open_successes_;
      if (half_open_successes_ >= cfg_.success_threshold) {
        state_ = BreakerState::kClosed;
        consecutive_failures_ = 0;
        half_open_successes_ = 0;
      }
    } else {
      consecutive_failures_ = 0;
    }
  }

  void record_failure() {
    if (cfg_.failure_threshold <= 0) {
      return;
    }
    const std::lock_guard<std::mutex> lock(mu_);
    if (state_ == BreakerState::kHalfOpen) {
      // Probe failed — re-open with a fresh timer.
      state_ = BreakerState::kOpen;
      opened_at_ = std::chrono::steady_clock::now();
      half_open_successes_ = 0;
      return;
    }
    ++consecutive_failures_;
    if (consecutive_failures_ >= cfg_.failure_threshold) {
      state_ = BreakerState::kOpen;
      opened_at_ = std::chrono::steady_clock::now();
    }
  }

  [[nodiscard]] BreakerState state() const {
    const std::lock_guard<std::mutex> lock(mu_);
    return state_;
  }

 private:
  CircuitBreakerConfig cfg_;
  mutable std::mutex mu_;
  BreakerState state_{BreakerState::kClosed};
  int consecutive_failures_{0};
  int half_open_successes_{0};
  std::chrono::steady_clock::time_point opened_at_{};
};

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init,hicpp-member-init)
HttpTestClient::HttpTestClient(const std::string& base_url, RetryPolicy retry,
                               CircuitBreakerConfig cb)
    : retry_(retry), cb_(std::make_unique<CircuitBreaker>(cb)) {
  // Parse "http://host:port" into host and port
  const std::regex url_re(R"(https?://([^:]+):(\d+))");
  // NOLINTNEXTLINE(misc-const-correctness) — mutated as regex_match output parameter
  std::smatch match = {};
  if (std::regex_match(base_url, match, url_re)) {
    host_ = match[1].str();
    try {
      port_ = std::stoi(match[2].str());
      // NOLINTNEXTLINE(bugprone-empty-catch) — catch rethrows, not empty
    } catch (const std::invalid_argument& e) {
      throw std::runtime_error("HttpTestClient: invalid port '" + match[2].str() +
                               "': " + e.what());
      // NOLINTNEXTLINE(bugprone-empty-catch) — catch rethrows, not empty
    } catch (const std::out_of_range& e) {
      throw std::runtime_error("HttpTestClient: port out of range '" + match[2].str() +
                               "': " + e.what());
    }
    if (port_ < 1 || port_ > 65535) {
      throw std::runtime_error("HttpTestClient: port out of valid range [1,65535]: " +
                               match[2].str());
    }
  } else {
    host_ = "localhost";
    port_ = 8080;
  }
  client_ = std::make_unique<httplib::Client>(host_, port_);
  client_->set_connection_timeout(kConnectionTimeoutSec);
  client_->set_read_timeout(kReadTimeoutSec);
}

HttpTestClient::~HttpTestClient() = default;

namespace {

/// Apply the retry-with-backoff envelope around an httplib call.
/// `fn` must return an `httplib::Result`. A `nullptr`-like result (status 0)
/// is the transient signal that triggers retry. Any concrete response —
/// including 4xx/5xx — is returned to the caller unchanged.
template <typename Fn>
HttpTestClient::Response run_with_retry(const RetryPolicy& policy,
                                        HttpTestClient::CircuitBreaker& breaker, Fn&& fn) {
  const int total = std::max(1, policy.max_retries + 1);
  for (int attempt = 0; attempt < total; ++attempt) {
    if (!breaker.allow_call()) {
      // Circuit is OPEN — short-circuit without touching the wire.
      return {0, {}};
    }

    auto res = fn();
    if (res) {
      breaker.record_success();
      return {res->status, parse_body(*res)};
    }

    breaker.record_failure();

    if (attempt == total - 1) {
      break;  // no sleep after the final attempt
    }
    // Exponential backoff with jitter: min(base * mult^attempt, cap) * U(0.5, 1.5).
    double delay = static_cast<double>(policy.base_delay_ms);
    for (int i = 0; i < attempt; ++i) {
      delay *= policy.backoff_mult;
    }
    delay = std::min(delay, static_cast<double>(policy.max_delay_ms));
    delay *= jitter_factor();
    std::this_thread::sleep_for(
        std::chrono::milliseconds{static_cast<int>(delay)});
  }
  return {0, {}};
}

}  // namespace

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
HttpTestClient::Response HttpTestClient::get(const std::string& path) {
  return run_with_retry(retry_, *cb_, [&] { return client_->Get(path); });
}

HttpTestClient::Response HttpTestClient::post(const std::string& path, const nlohmann::json& body) {
  return post_raw(path, body.dump(), "application/json");
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
HttpTestClient::Response HttpTestClient::del(const std::string& path) {
  return run_with_retry(retry_, *cb_, [&] { return client_->Delete(path); });
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters,readability-convert-member-functions-to-static)
HttpTestClient::Response HttpTestClient::post_raw(const std::string& path, const std::string& body,
                                                  const std::string& content_type) {
  return run_with_retry(retry_, *cb_,
                        [&] { return client_->Post(path, body, content_type); });
}

bool HttpTestClient::is_healthy() {
  auto [status, body] = get("/v1/health");
  return status == 200 && body.value("status", "") == "ok";
}

HttpTestClient::BreakerState HttpTestClient::test_breaker_state() const { return cb_->state(); }

}  // namespace projectcharybdis
