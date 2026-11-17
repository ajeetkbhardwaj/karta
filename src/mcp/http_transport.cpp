// Karta — MCP HTTP transport using cpp-httplib (OpenSSL backend).

#include "karta/mcp/mcp.h"

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <sstream>
#include <thread>
#include <type_traits>

namespace karta::mcp {

namespace {

std::string trimHeaderValue(const std::string& value) {
  const std::size_t begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) return "";
  const std::size_t end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::string lower_ascii(std::string value) {
  for (char& ch : value) {
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
  }
  return value;
}

Json jsonFromRpcId(const JsonRpcId& id) {
  Json out = Json::object();
  std::visit([&](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, int64_t>) {
      out = Json(static_cast<long long>(value));
    } else {
      out = Json(std::string(value));
    }
  }, id);
  return out;
}

Json jsonFromRequest(const JsonRpcRequest& req) {
  Json out = Json::object();
  out.set("jsonrpc", req.jsonrpc);
  out.set("id", jsonFromRpcId(req.id));
  out.set("method", req.method);
  if (!req.params.is_null()) out.set("params", req.params);
  return out;
}

Json jsonFromNotification(const JsonRpcNotification& notif) {
  Json out = Json::object();
  out.set("jsonrpc", notif.jsonrpc);
  out.set("method", notif.method);
  if (!notif.params.is_null()) out.set("params", notif.params);
  return out;
}

bool isJsonRpcLike(const Json& json) {
  if (!json.is_object()) return false;
  const std::string jsonrpc = json["jsonrpc"].as_string();
  if (jsonrpc != "2.0") return false;
  if (json["method"].is_string()) {
    return !json["id"].is_number() && !json["id"].is_string() ||
           (json["id"].is_number() || json["id"].is_string());
  }
  return (json["id"].is_number() || json["id"].is_string());
}

void handleJsonRpcPayload(const Json& responseJson, const std::function<void(const JsonRpcMessage&)>& onMessage) {
  if (!isJsonRpcLike(responseJson)) return;

  const bool hasId = responseJson["id"].is_number() || responseJson["id"].is_string();
  const bool hasMethod = responseJson["method"].is_string();

  if (hasId && hasMethod) {
    JsonRpcRequest req;
    req.jsonrpc = responseJson["jsonrpc"].as_string();
    req.method = responseJson["method"].as_string();
    req.params = responseJson["params"];
    if (responseJson["id"].is_number()) {
      req.id = static_cast<int64_t>(responseJson["id"].as_int());
    } else {
      req.id = responseJson["id"].as_string();
    }
    if (onMessage) onMessage(req);
    return;
  }

  if (hasId) {
    JsonRpcResponse response;
    response.jsonrpc = responseJson["jsonrpc"].as_string();
    if (responseJson["id"].is_number()) {
      response.id = static_cast<int64_t>(responseJson["id"].as_int());
    } else {
      response.id = responseJson["id"].as_string();
    }
    response.result = responseJson["result"];
    if (responseJson["error"].is_object()) {
      JsonRpcResponse::Error err;
      err.code = static_cast<int>(responseJson["error"]["code"].as_int());
      err.message = responseJson["error"]["message"].as_string();
      err.data = responseJson["error"]["data"];
      response.error = err;
    }
    if (onMessage) onMessage(response);
    return;
  }

  JsonRpcNotification notif;
  notif.jsonrpc = responseJson["jsonrpc"].as_string();
  notif.method = responseJson["method"].as_string();
  notif.params = responseJson["params"];
  if (onMessage) onMessage(notif);
}

}  // namespace

class HttpTransportImpl : public McpTransport {
 public:
  explicit HttpTransportImpl(HttpTransportOptions options)
      : options_(std::move(options)) {
    protocolVersion_ = options_.protocolVersion.value_or("2025-11-25");
  }

  ~HttpTransportImpl() override { close(); }

  void start() override {
    if (started_) throw std::runtime_error("Transport already started");
    if (closed_) throw std::runtime_error("Transport closed");
    started_ = true;
  }

  void send(const JsonRpcMessage& message) override {
    if (!started_ || closed_) {
      throw std::runtime_error("Transport not started or closed");
    }

    Json payload = Json::object();
    if (std::holds_alternative<JsonRpcRequest>(message)) {
      const auto& req = std::get<JsonRpcRequest>(message);
      payload = jsonFromRequest(req);
    } else if (std::holds_alternative<JsonRpcNotification>(message)) {
      const auto& notif = std::get<JsonRpcNotification>(message);
      payload = jsonFromNotification(notif);
    } else {
      throw std::runtime_error("Cannot send response via transport");
    }

    sendPost(payload);
  }

  void close() override {
    if (closed_) return;
    closed_ = true;
    started_ = false;
    if (onClose) onClose();
  }

  bool isStarted() const override { return started_; }
  bool isClosed() const override { return closed_; }

  const std::string& sessionId() const { return sessionId_; }

 private:
  struct ParsedUrl {
    std::string scheme;
    std::string host_port;
    std::string path;
  };

  ParsedUrl parseUrl(const std::string& url) {
    ParsedUrl result;
    const std::size_t scheme_end = url.find("://");
    if (scheme_end != std::string::npos) {
      result.scheme = url.substr(0, scheme_end);
      const std::size_t host_start = scheme_end + 3;
      const std::size_t path_start = url.find('/', host_start);
      if (path_start != std::string::npos) {
        result.host_port = url.substr(host_start, path_start - host_start);
        result.path = url.substr(path_start);
      } else {
        result.host_port = url.substr(host_start);
        result.path = "/";
      }
    } else {
      result.scheme = "https";
      const std::size_t path_start = url.find('/');
      if (path_start != std::string::npos) {
        result.host_port = url.substr(0, path_start);
        result.path = url.substr(path_start);
      } else {
        result.host_port = url;
        result.path = "/";
      }
    }
    if (result.path.empty()) result.path = "/";
    return result;
  }

  httplib::Headers buildHeaders() {
    httplib::Headers headers;
    headers.emplace("Content-Type", "application/json");
    headers.emplace("Accept", "application/json");
    if (!protocolVersion_.empty()) {
      headers.emplace("MCP-Protocol-Version", protocolVersion_);
    }
    if (!sessionId_.empty()) {
      headers.emplace("Mcp-Session-Id", sessionId_);
    }
    for (const auto& [k, v] : options_.headers) {
      headers.emplace(k, v);
    }
    if (options_.authProvider) {
      const std::string token = options_.authProvider.value()();
      if (!token.empty()) {
        headers.emplace("Authorization", "Bearer " + token);
      }
    }
    return headers;
  }

  void extractSessionId(const httplib::Headers& headers) {
    auto it = headers.find("mcp-session-id");
    if (it == headers.end()) it = headers.find("Mcp-Session-Id");
    if (it == headers.end()) it = headers.find("MCP-Session-Id");
    if (it == headers.end()) return;
    sessionId_ = trimHeaderValue(it->second);
  }

  void sendPost(const Json& json) {
    const std::string payload = json.dump();
    const httplib::Headers headers = buildHeaders();
    const ParsedUrl parsed = parseUrl(options_.url);
    const bool use_tls = lower_ascii(parsed.scheme) == "https";

    if (use_tls) {
      auto client = std::make_unique<httplib::SSLClient>(parsed.host_port);
      client->set_ca_cert_path("/etc/ssl/certs/ca-certificates.crt");
      client->set_connection_timeout(30, 0);
      client->set_read_timeout(30, 0);
      client->set_write_timeout(30, 0);
      client->set_follow_location(false);

      const auto res = client->Post(parsed.path, headers, payload, "application/json");
      if (!res) {
        const auto err = res.error();
        std::string err_msg;
        if (err == httplib::Error::Canceled) err_msg = "aborted";
        else if (err == httplib::Error::ConnectionTimeout || err == httplib::Error::Read) err_msg = "timeout";
        else if (err == httplib::Error::SSLConnection) err_msg = "SSL connection failed";
        else err_msg = httplib::to_string(err);
        throw std::runtime_error("HTTP POST failed: " + err_msg);
      }
      if (res->status < 200 || res->status >= 300) {
        throw std::runtime_error("HTTP POST failed: HTTP " + std::to_string(res->status));
      }
      extractSessionId(res->headers);
      if (!res->body.empty()) {
        try {
          const Json responseJson = Json::parse(res->body);
          handleJsonRpcPayload(responseJson, onMessage);
        } catch (...) {
          // Ignore invalid JSON payloads.
        }
      }
      return;
    }

    auto client = std::make_unique<httplib::Client>(parsed.host_port);
    client->set_connection_timeout(30, 0);
    client->set_read_timeout(30, 0);
    client->set_write_timeout(30, 0);
    client->set_follow_location(false);

    const auto res = client->Post(parsed.path, headers, payload, "application/json");
    if (!res) {
      const auto err = res.error();
      std::string err_msg;
      if (err == httplib::Error::Canceled) err_msg = "aborted";
      else if (err == httplib::Error::ConnectionTimeout || err == httplib::Error::Read) err_msg = "timeout";
      else if (err == httplib::Error::SSLConnection) err_msg = "SSL connection failed";
      else err_msg = httplib::to_string(err);
      throw std::runtime_error("HTTP POST failed: " + err_msg);
    }
    if (res->status < 200 || res->status >= 300) {
      throw std::runtime_error("HTTP POST failed: HTTP " + std::to_string(res->status));
    }
    extractSessionId(res->headers);
    if (!res->body.empty()) {
      try {
        const Json responseJson = Json::parse(res->body);
        handleJsonRpcPayload(responseJson, onMessage);
      } catch (...) {
        // Ignore invalid JSON payloads.
      }
    }
  }

  HttpTransportOptions options_;
  std::string protocolVersion_;
  std::string sessionId_;
  bool started_ = false;
  bool closed_ = false;
};

std::unique_ptr<McpTransport> createHttpTransport(const HttpTransportOptions& options) {
  return std::make_unique<HttpTransportImpl>(options);
}

}  // namespace karta::mcp