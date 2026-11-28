// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceFile.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>
#if defined(ARDUINO) && !defined(SIMULATOR)
#include <mbedtls/sha256.h>
#else
#include <openssl/sha.h>
#endif

namespace booksync::storage {
struct SourceFile::Hash {
#if defined(ARDUINO) && !defined(SIMULATOR)
  mbedtls_sha256_context state;
  Hash() { mbedtls_sha256_init(&state); }
  ~Hash() { mbedtls_sha256_free(&state); }
  bool begin() { return mbedtls_sha256_starts(&state, 0) == 0; }
  bool add(const uint8_t* p, size_t n) { return mbedtls_sha256_update(&state, p, n) == 0; }
  bool finish(uint8_t* p) { return mbedtls_sha256_finish(&state, p) == 0; }
#else
  SHA256_CTX state{};
  bool begin() { return SHA256_Init(&state) == 1; }
  bool add(const uint8_t* p, size_t n) { return SHA256_Update(&state, p, n) == 1; }
  bool finish(uint8_t* p) { return SHA256_Final(p, &state) == 1; }
#endif
};
SourceFile::SourceFile(BindingView binding, Closed closed, void* context)
    : source_(binding.source), closed_(closed), context_(context) {
  valid_ = valid(source_) && closed && !binding.scope.server.empty() && !binding.scope.account.empty() &&
           !binding.path.empty() && binding.path.front() == '/' && binding.path.size() < path_.size() &&
           binding.path.find('\0') == binding.path.npos;
  if (valid_) std::memcpy(path_.data(), binding.path.data(), binding.path.size());
}
SourceFile::~SourceFile() = default;
bool SourceFile::verify(const SourceIdentity& source, std::string_view path) {
  if (!valid_ || source != source_ || path != path_.data() || !closed_(context_)) {
    LOG_ERR("BSE", "Closed source binding unavailable");
    return false;
  }
  if (!hash_) {
    // At most 256 bytes, retained and reused for this optional worker object's
    // lifetime. Fallible allocation avoids a hash context on nested task frames.
    static_assert(sizeof(Hash) <= 256);
    hash_ = makeUniqueNoThrow<Hash>();
    if (!hash_) {
      LOG_ERR("BSE", "Cannot allocate source verification state");
      return false;
    }
  }
  if (!hash_->begin()) {
    LOG_ERR("BSE", "Source digest initialization failed");
    return false;
  }
  FsFile file = Storage.open(path_.data(), O_RDONLY);
  if (!file) {
    LOG_ERR("BSE", "Source verification open failed");
    return false;
  }
#if SIZE_MAX < UINT64_MAX
  const auto size = file.fileSize64();
#else
  const auto size = file.fileSize();
#endif
  auto remaining = size;
  bool ok = true;
  while (remaining) {
    const auto count = std::min<uint64_t>(remaining, buffer_.size());
    if (!closed_(context_) || file.read(buffer_.data(), count) != static_cast<int>(count) ||
        !hash_->add(buffer_.data(), count)) {
      ok = false;
      break;
    }
    remaining -= count;
  }
  // Check both framing and content; a short read or changed length cannot become
  // proof, even when the available prefix happened to match a saved digest.
  uint8_t extra;
#if SIZE_MAX < UINT64_MAX
  ok = ok && file.fileSize64() == size && file.read(&extra, 1) == 0;
#else
  ok = ok && file.fileSize() == size && file.read(&extra, 1) == 0;
#endif
  const bool closed = file.close();
  Revision actual;
  ok = ok && closed && closed_(context_) && hash_->finish(actual.bytes.data()) && actual == source_.revision;
  if (!ok) LOG_ERR("BSE", "Source revision or closed-file proof changed");
  return ok;
}
}  // namespace booksync::storage
