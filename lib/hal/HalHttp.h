// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <SecureHttpClient.h>

#include <memory>
#include <string>
#include <string_view>

struct HalHttpHeader {
  std::string_view name, value;
};
// Generic bounded response cursor. Caller serializes the spool and supplies an
// atomic cancellation predicate; no application/account policy enters the HAL.
// Header-only so native simulator profiles can reuse it while replacing the
// board-specific HAL library. The sole production caller owns one instance.
class HalHttp final {
 public:
  using Permitted = bool (*)();
  ~HalHttp() { reset(); }
  int open(const std::string& url, const char* method, std::string_view payload, const HalHttpHeader* headers,
           size_t count, const char* spool, size_t maximum, Permitted permitted) {
    close();
    if (!permitted || !permitted() || !spool || !maximum || maximum > 4 * 1024 * 1024 || count > 8) return 0;
    if (!http_) {
      // <=2 KiB SDK control object, too large for the task stack. Reused for all
      // requests in this worker session; reset releases it before reading.
      static_assert(sizeof(freeink::SecureHttpClient) <= 2048);
      http_ = makeUniqueNoThrow<freeink::SecureHttpClient>();
      if (!http_) {
        LOG_ERR("HTTP", "Cannot allocate bounded HTTP session");
        return 0;
      }
    }
    permitted_ = permitted;
    if (!http_->begin(url)) {
      LOG_ERR("HTTP", "Rejected request origin");
      return 0;
    }
    http_->setTimeout(15000);
    http_->setFollowRedirects(0);
    http_->setReuse(false);
    // Retain the existing downloader/SecureNet policy: no platform CA bundle.
    http_->setInsecure();
    http_->setUserAgent("BookSync");
    for (size_t i = 0; i < count; ++i) http_->addHeader(std::string(headers[i].name), std::string(headers[i].value));
    body_ = Storage.open(spool, O_WRONLY | O_CREAT | O_TRUNC);
    if (!body_) {
      LOG_ERR("HTTP", "Cannot create response spool");
      close();
      return 0;
    }
    size_t bytes = 0;
    bool written = true;
    const auto status = http_->sendRequest(
        method, reinterpret_cast<const uint8_t*>(payload.data()), payload.size(),
        [&](const uint8_t* data, size_t size) {
          if (!permitted() || size > maximum - bytes) return false;
          if (body_.write(data, size) != size) {
            written = false;
            return false;
          }
          bytes += size;
          return true;
        },
        [permitted] { return !permitted(); });
    complete_ = written && http_->responseComplete() && !http_->callbackAborted() && !http_->aborted();
    if (!body_.close()) {
      LOG_ERR("HTTP", "Response spool close failed");
      complete_ = false;
    }
    http_->end();
    http_->begin("");  // Release SDK-owned private header references.
    status_ = complete_ ? status : 0;
    size_ = bytes;
    if (complete_) {
      body_ = Storage.open(spool, O_RDONLY);
      if (!body_) {
        LOG_ERR("HTTP", "Cannot reopen complete response spool");
        complete_ = false;
        status_ = 0;
      }
    } else {
      LOG_ERR("HTTP", "Response incomplete; no body cursor published");
    }
    return status_;
  }
  int read(char* out, size_t size) {
    if (!complete_ || !body_ || !permitted_ || !permitted_()) return -1;
    const auto bytes = body_.read(out, size);
    if (bytes > 0) read_ += bytes;
    return bytes;
  }
  int finish() const { return complete_ && body_ && read_ == size_ ? status_ : 0; }
  void close() {
    body_.close();
    if (http_) {
      http_->end();
      http_->begin("");
    }
    complete_ = false;
    status_ = 0;
    read_ = size_ = 0;
    permitted_ = nullptr;
  }
  void reset() {
    close();
    http_.reset();
  }

 private:
  std::unique_ptr<freeink::SecureHttpClient> http_;
  FsFile body_;
  Permitted permitted_ = nullptr;
  int status_ = 0;
  bool complete_ = false;
  size_t read_ = 0, size_ = 0;
};
