// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../../../lib/hal/HalHttp.h"
#include "LiveAdapter.h"
#include "booksync/system/NetworkPhase.h"

namespace booksync::server {
namespace {
bool permitted() { return networkAdmission().permitted(); }
class DeviceHttp final : public HttpPort {
  HalHttp http_;

 public:
  int open(const HttpLogin& login, const char* method, std::string_view path, std::string_view payload,
           size_t maximum) override {
    close();
    if (!permitted() || !maximum || maximum > Catalog::MaxPageBytes || path.empty() || path.front() != '/' ||
        path.find_first_of(std::string_view("\r\n\0", 3)) != path.npos)
      return 0;
    // Evidence/cursor allocations may have followed initial phase admission.
    const auto memory = memorySnapshot();
    if (memory.internalFree < Admission::minimumFree || memory.internalLargest < Admission::minimumLargest) {
      LOG_ERR("BSH", "Sync deferred: internal memory below admission floor");
      return 0;
    }
    std::string url(login.origin.data());
    if (url.find("://") == url.npos) url.insert(0, "https://");
    while (!url.empty() && url.back() == '/') url.pop_back();
    if (url.find_first_of("?#@") != url.npos) return 0;
    constexpr std::string_view suffix = "/api/v1/koreader";
    if (url.size() >= suffix.size() && url.compare(url.size() - suffix.size(), suffix.size(), suffix) == 0)
      url.resize(url.size() - suffix.size());
    else if (url.size() >= 7 && url.compare(url.size() - 7, 7, "/api/v1") == 0)
      url.resize(url.size() - 7);
    url += "/api/v1";
    url.append(path.data(), path.size());
    if (login.authorizationUser[0] && url.rfind("https://", 0) != 0) return 0;
    // Six borrowed header pairs (~192 bytes on host) avoid a second heap-backed
    // header list; the HAL's SDK copies them only for this bounded request.
    HalHttpHeader headers[6]{
        {"Accept", "application/json"}, {"x-auth-user", login.user.data()}, {"x-auth-key", login.secret.data()}};
    size_t count = 3;
    if (login.authorizationUser[0]) {
      headers[count++] = {"CF-Access-Client-Id", login.authorizationUser.data()};
      headers[count++] = {"CF-Access-Client-Secret", login.authorizationSecret.data()};
    }
    if (!payload.empty()) headers[count++] = {"Content-Type", "application/json"};
    return http_.open(url, method, payload, headers, count, "/.booksync/http.body.tmp", maximum, permitted);
  }
  int read(char* out, size_t size) override { return http_.read(out, size); }
  int finish() override { return http_.finish(); }
  void close() override { http_.close(); }
  void release() override { http_.reset(); }
};
}  // namespace
HttpPort& deviceHttpPort() {
  static DeviceHttp port;
  return port;
}
}  // namespace booksync::server
