// Karta — HTTP transport using cpp-httplib (header-only, OpenSSL backend).
// Replaces libcurl implementation.

#include "karta/ai/http.h"
#include "httplib.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <unistd.h>

namespace karta {

std::mutex http_mutex;

namespace {

bool is_cancelled(const std::atomic<bool>* cancel) {
  return cancel && cancel->load();
}

httplib::Headers to_httplib_headers(const std::vector<std::string>& headers) {
  httplib::Headers h;
  for (const auto& header : headers) {
    auto pos = header.find(':');
    if (pos != std::string::npos) {
      std::string key = header.substr(0, pos);
      std::string value = header.substr(pos + 1);
      size_t start = value.find_first_not_of(' ');
      if (start != std::string::npos) value = value.substr(start);
      h.emplace(std::move(key), std::move(value));
    }
  }
  return h;
}

struct ParsedUrl {
  std::string scheme;
  std::string host_port;
  std::string path;
};

ParsedUrl parse_url(const std::string& url) {
  ParsedUrl result;
  size_t scheme_end = url.find("://");
  if (scheme_end != std::string::npos) {
    result.scheme = url.substr(0, scheme_end);
    size_t host_start = scheme_end + 3;
    size_t path_start = url.find('/', host_start);
    if (path_start != std::string::npos) {
      result.host_port = url.substr(host_start, path_start - host_start);
      result.path = url.substr(path_start);
    } else {
      result.host_port = url.substr(host_start);
      result.path = "/";
    }
  } else {
    result.scheme = "https";
    size_t path_start = url.find('/');
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

std::string get_redirect_url(const httplib::Response& res) {
  auto it = res.headers.find("Location");
  if (it != res.headers.end()) return it->second;
  return "";
}

bool is_redirect_status(int status) {
  return (status >= 300 && status < 400) && 
         (status == 301 || status == 302 || status == 303 || status == 307 || status == 308);
}

std::string get_ca_cert_path() {
#if defined(__APPLE__)
  // macOS: Check common OpenSSL cert paths (Homebrew, MacPorts, system)
  static const char* paths[] = {
    "/usr/local/etc/openssl@3/cert.pem",      // Homebrew ARM
    "/opt/homebrew/etc/openssl@3/cert.pem",   // Homebrew ARM (alternative)
    "/usr/local/etc/openssl/cert.pem",        // Homebrew x86_64
    "/opt/local/etc/openssl3/cert.pem",       // MacPorts
    "/etc/ssl/cert.pem",                      // System fallback
    "/etc/ssl/certs/ca-certificates.crt",     // Linux fallback
    nullptr
  };
  for (int i = 0; paths[i]; ++i) {
    if (access(paths[i], R_OK) == 0) return paths[i];
  }
  return "";  // Let OpenSSL use default system store
#elif defined(_WIN32)
  // Windows: use default system store
  return "";
#else
  // Linux: common paths
  static const char* paths[] = {
    "/etc/ssl/certs/ca-certificates.crt",     // Debian/Ubuntu
    "/etc/pki/tls/certs/ca-bundle.crt",       // RHEL/CentOS/Fedora
    "/etc/ssl/ca-bundle.pem",                 // OpenSUSE
    "/etc/ssl/cert.pem",                      // Arch
    nullptr
  };
  for (int i = 0; paths[i]; ++i) {
    if (access(paths[i], R_OK) == 0) return paths[i];
  }
  return "";
#endif
}

} // namespace

HttpResponse http_post_stream(const std::string& url, const std::vector<std::string>& headers,
                              const std::string& body, const ChunkCallback& on_chunk,
                              const std::atomic<bool>* cancel) {
  // Prepare request data outside mutex
  HttpResponse result;
  ParsedUrl parsed = parse_url(url);
  httplib::Headers hdrs = to_httplib_headers(headers);
  
  // Streaming callback - receives chunks as they arrive
  std::mutex result_mutex;
  httplib::ContentReceiver stream_callback = [&](const char* data, size_t len) -> bool {
    if (is_cancelled(cancel)) return false;
    if (!on_chunk(data, len)) return false;
    std::lock_guard<std::mutex> rlock(result_mutex);
    result.body.append(data, len);
    return true;
  };
  
  // Execute HTTP request WITHOUT holding global mutex (allows concurrent requests)
  if (parsed.scheme == "https") {
    auto cli = std::make_unique<httplib::SSLClient>(parsed.host_port);
    std::string ca_path = get_ca_cert_path();
    if (!ca_path.empty()) cli->set_ca_cert_path(ca_path);
    cli->set_connection_timeout(30, 0);
    cli->set_read_timeout(60, 0);
    cli->set_write_timeout(60, 0);
    cli->set_follow_location(false);
    
    auto res = cli->Post(parsed.path, hdrs, body, "application/json", stream_callback);
    
    if (!res) {
      auto err = res.error();
      if (err == httplib::Error::Canceled) result.error = "aborted";
      else if (err == httplib::Error::ConnectionTimeout || err == httplib::Error::Read) result.error = "timeout";
      else if (err == httplib::Error::SSLConnection) result.error = "SSL connection failed";
      else result.error = httplib::to_string(err);
      return result;
    }
    
    result.status = res->status;
    
    if (result.status < 200 || result.status >= 300) {
      result.error = "HTTP " + std::to_string(result.status);
      return result;
    }
    
    return result;
  } else {
    auto cli = std::make_unique<httplib::Client>(parsed.host_port);
    cli->set_connection_timeout(30, 0);
    cli->set_read_timeout(60, 0);
    cli->set_write_timeout(60, 0);
    cli->set_follow_location(false);
    
    auto res = cli->Post(parsed.path, hdrs, body, "application/json", stream_callback);
    
    if (!res) {
      auto err = res.error();
      if (err == httplib::Error::Canceled) result.error = "aborted";
      else if (err == httplib::Error::ConnectionTimeout || err == httplib::Error::Read) result.error = "timeout";
      else if (err == httplib::Error::SSLConnection) result.error = "SSL connection failed";
      else result.error = httplib::to_string(err);
      return result;
    }
    
    result.status = res->status;
    
    if (result.status < 200 || result.status >= 300) {
      result.error = "HTTP " + std::to_string(result.status);
      return result;
    }
    
    return result;
  }
}

HttpResponse http_get(const std::string& url, const std::vector<std::string>& headers,
                      const std::atomic<bool>* cancel,
                      const RedirectCallback& on_redirect) {
  std::lock_guard<std::mutex> lock(http_mutex);
  
  HttpResponse result;
  ParsedUrl parsed = parse_url(url);
  httplib::Headers hdrs = to_httplib_headers(headers);
  
  std::string current_scheme = parsed.scheme;
  std::string current_host = parsed.host_port;
  std::string current_path = parsed.path;
  
  for (int redirect_count = 0; redirect_count <= 10; ++redirect_count) {
    if (is_cancelled(cancel)) {
      result.error = "aborted";
      return result;
    }
    
    if (current_scheme == "https") {
      auto cli = std::make_unique<httplib::SSLClient>(current_host);
      std::string ca_path = get_ca_cert_path();
      if (!ca_path.empty()) cli->set_ca_cert_path(ca_path);
      cli->set_connection_timeout(30, 0);
      cli->set_read_timeout(60, 0);
      cli->set_write_timeout(60, 0);
      cli->set_follow_location(false);
      
      auto res = cli->Get(current_path, hdrs);
      
      if (!res) {
        auto err = res.error();
        if (err == httplib::Error::Canceled) result.error = "aborted";
        else if (err == httplib::Error::ConnectionTimeout || err == httplib::Error::Read) result.error = "timeout";
        else if (err == httplib::Error::SSLConnection) result.error = "SSL connection failed";
        else result.error = httplib::to_string(err);
        return result;
      }
      
      result.status = res->status;
      result.body = res->body;
      
      if (result.status < 200 || result.status >= 300) {
        result.error = "HTTP " + std::to_string(result.status) + ": " + result.body;
        return result;
      }
      
      // Check for redirect
      if (is_redirect_status(result.status)) {
        std::string redirect_url = get_redirect_url(*res);
        if (redirect_url.empty()) {
          result.error = "redirect without Location header";
          return result;
        }
        
        if (on_redirect && !on_redirect(redirect_url)) {
          result.error = "redirect blocked by SSRF protection: " + redirect_url;
          return result;
        }
        
        ParsedUrl next = parse_url(redirect_url);
        current_scheme = next.scheme;
        current_host = next.host_port;
        current_path = next.path;
        continue;
      }
      
      return result;
    } else {
      auto cli = std::make_unique<httplib::Client>(current_host);
      cli->set_connection_timeout(30, 0);
      cli->set_read_timeout(60, 0);
      cli->set_write_timeout(60, 0);
      cli->set_follow_location(false);
      
      auto res = cli->Get(current_path, hdrs);
      
      if (!res) {
        auto err = res.error();
        if (err == httplib::Error::Canceled) result.error = "aborted";
        else if (err == httplib::Error::ConnectionTimeout || err == httplib::Error::Read) result.error = "timeout";
        else if (err == httplib::Error::SSLConnection) result.error = "SSL connection failed";
        else result.error = httplib::to_string(err);
        return result;
      }
      
      result.status = res->status;
      result.body = res->body;
      
      if (result.status < 200 || result.status >= 300) {
        result.error = "HTTP " + std::to_string(result.status) + ": " + result.body;
        return result;
      }
      
      // Check for redirect
      if (is_redirect_status(result.status)) {
        std::string redirect_url = get_redirect_url(*res);
        if (redirect_url.empty()) {
          result.error = "redirect without Location header";
          return result;
        }
        
        if (on_redirect && !on_redirect(redirect_url)) {
          result.error = "redirect blocked by SSRF protection: " + redirect_url;
          return result;
        }
        
        ParsedUrl next = parse_url(redirect_url);
        current_scheme = next.scheme;
        current_host = next.host_port;
        current_path = next.path;
        continue;
      }
      
      return result;
    }
  }
  
  result.error = "too many redirects";
  return result;
}

}  // namespace karta