#include "projectcharybdis/http_test_client.hpp"

#include <httplib.h>
#include <iostream>
#include <regex>
#include <stdexcept>

namespace projectcharybdis {

HttpTestClient::HttpTestClient(const std::string& base_url) {
  // Parse "http://host:port" into host and port
  std::regex url_re(R"(https?://([^:]+):(\d+))");
  std::smatch match;
  if (std::regex_match(base_url, match, url_re)) {
    host_ = match[1].str();
    const std::string port_str = match[2].str();
    try {
      const int parsed = std::stoi(port_str);
      if (parsed < 0 || parsed > 65535) {
        throw std::invalid_argument("port out of range");
      }
      port_ = parsed;
    } catch (const std::out_of_range&) {
      throw std::invalid_argument("Invalid port in URL '" + base_url + "': '" + port_str +
                                  "' is not in range [0, 65535]");
    } catch (const std::invalid_argument&) {
      throw std::invalid_argument("Invalid port in URL '" + base_url + "': '" + port_str +
                                  "' is not in range [0, 65535]");
    }
  } else {
    host_ = "localhost";
    port_ = 8080;
  }
}

HttpTestClient::Response HttpTestClient::get(const std::string& path) {
  httplib::Client cli(host_, port_);
  cli.set_connection_timeout(5);
  cli.set_read_timeout(10);

  auto res = cli.Get(path);
  if (!res) return {0, {}};

  nlohmann::json body;
  try {
    body = nlohmann::json::parse(res->body);
  } catch (...) {
    body = {{"raw", res->body}};
  }
  return {res->status, body};
}

HttpTestClient::Response HttpTestClient::post(const std::string& path, const nlohmann::json& body) {
  return post_raw(path, body.dump(), "application/json");
}

HttpTestClient::Response HttpTestClient::del(const std::string& path) {
  httplib::Client cli(host_, port_);
  cli.set_connection_timeout(5);
  cli.set_read_timeout(10);

  auto res = cli.Delete(path);
  if (!res) return {0, {}};

  nlohmann::json resp_body;
  try {
    resp_body = nlohmann::json::parse(res->body);
  } catch (...) {
    resp_body = {{"raw", res->body}};
  }
  return {res->status, resp_body};
}

HttpTestClient::Response HttpTestClient::post_raw(const std::string& path, const std::string& body,
                                                  const std::string& content_type) {
  httplib::Client cli(host_, port_);
  cli.set_connection_timeout(5);
  cli.set_read_timeout(10);

  auto res = cli.Post(path, body, content_type);
  if (!res) return {0, {}};

  nlohmann::json resp_body;
  try {
    resp_body = nlohmann::json::parse(res->body);
  } catch (...) {
    resp_body = {{"raw", res->body}};
  }
  return {res->status, resp_body};
}

bool HttpTestClient::is_healthy() {
  auto [status, body] = get("/v1/health");
  return status == 200 && body.value("status", "") == "ok";
}

}  // namespace projectcharybdis
