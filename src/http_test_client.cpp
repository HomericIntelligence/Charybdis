#include "projectcharybdis/http_test_client.hpp"

#include <httplib.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <regex>
#include <stdexcept>
#include <string>

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
}  // namespace

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init,hicpp-member-init)
HttpTestClient::HttpTestClient(const std::string& base_url) {
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

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
HttpTestClient::Response HttpTestClient::get(const std::string& path) {
  auto res = client_->Get(path);
  if (!res) {
    return {0, {}};
  }
  return {res->status, parse_body(*res)};
}

HttpTestClient::Response HttpTestClient::post(const std::string& path, const nlohmann::json& body) {
  return post_raw(path, body.dump(), "application/json");
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
HttpTestClient::Response HttpTestClient::del(const std::string& path) {
  auto res = client_->Delete(path);
  if (!res) {
    return {0, {}};
  }
  return {res->status, parse_body(*res)};
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters,readability-convert-member-functions-to-static)
HttpTestClient::Response HttpTestClient::post_raw(const std::string& path, const std::string& body,
                                                  const std::string& content_type) {
  auto res = client_->Post(path, body, content_type);
  if (!res) {
    return {0, {}};
  }
  return {res->status, parse_body(*res)};
}

bool HttpTestClient::is_healthy() {
  auto [status, body] = get("/v1/health");
  return status == 200 && body.value("status", "") == "ok";
}

}  // namespace projectcharybdis
