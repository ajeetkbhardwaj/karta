// Karta — HTTP transport using cpp-httplib (OpenSSL backend).
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace karta {

struct HttpResponse {
  long status = 0;
  std::string body;
  std::string error;

  bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

using ChunkCallback = std::function<bool(const char* data, std::size_t length)>;
using RedirectCallback = std::function<bool(const std::string& new_url)>;

extern std::mutex http_mutex;

// Blocks until the response completes, forwarding body bytes to `on_chunk`.
// `cancel` is polled; setting it aborts the transfer.
HttpResponse http_post_stream(const std::string& url, const std::vector<std::string>& headers,
                              const std::string& body, const ChunkCallback& on_chunk,
                              const std::atomic<bool>* cancel = nullptr);

// Simple GET that buffers the whole response body (provider model discovery).
// `on_redirect` is called for each redirect; return false to abort.
HttpResponse http_get(const std::string& url, const std::vector<std::string>& headers,
                      const std::atomic<bool>* cancel = nullptr,
                      const RedirectCallback& on_redirect = nullptr);

} // namespace karta
