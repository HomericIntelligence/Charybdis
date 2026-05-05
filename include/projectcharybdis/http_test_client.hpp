#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace projectcharybdis {

/// Thin HTTP client for chaos/resilience GTest tests.
/// Wraps cpp-httplib for REST API interactions with Agamemnon.
class HttpTestClient {
 public:
  static constexpr std::size_t kMaxBodyBytes = 10 * 1024 * 1024;  // 10 MB

  explicit HttpTestClient(const std::string& base_url = "http://localhost:8080");

  /// GET request, returns {status_code, body_json}
  struct Response {
    int status;
    nlohmann::json body;
  };

  Response get(const std::string& path);
  Response post(const std::string& path, const nlohmann::json& body = {});
  Response del(const std::string& path);

  /// POST with raw string body (for malformed payload tests)
  Response post_raw(const std::string& path, const std::string& body,
                    const std::string& content_type = "application/json");

  bool is_healthy();

 private:
  std::string host_;
  int port_;
};

}  // namespace projectcharybdis
