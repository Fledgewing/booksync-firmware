// SPDX-License-Identifier: GPL-3.0-or-later
#include "Identity.h"
#if !defined(SIMULATOR)
#include <Logging.h>
#include <nvs.h>

#include <cstring>
#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#elif defined(__linux__)
#include <openssl/sha.h>
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#else
#include <mbedtls/sha256.h>
#endif

namespace booksync::import {
namespace {
struct Field {
  const char* space;
  const char* key;
  nvs_type_t type;
};
// Exact 0.11/F10 persistent names and types; no identity is renamed or derived.
static const Field fields[] = {
    {"microlink", "machine_pri", NVS_TYPE_BLOB}, {"microlink", "machine_pub", NVS_TYPE_BLOB},
    {"microlink", "wg_private", NVS_TYPE_BLOB},  {"microlink", "wg_public", NVS_TYPE_BLOB},
    {"microlink", "disco_pri", NVS_TYPE_BLOB},   {"microlink", "disco_pub", NVS_TYPE_BLOB},
    {"tailnet", "enabled", NVS_TYPE_U8},         {"tailnet", "joined", NVS_TYPE_U8},
    {"tailnet", "pin_ip", NVS_TYPE_U32},         {"tailnet", "relay_mode", NVS_TYPE_U32},
    {"tailnet", "relay_rgn", NVS_TYPE_U32},      {"tailnet", "srv_rgn", NVS_TYPE_U32},
    {"tailnet", "srv_ip", NVS_TYPE_U32},         {"tailnet", "login_url", NVS_TYPE_STR},
    {"ml_wg", "ts_floor", NVS_TYPE_U64},         {"microlink", "ctl_cache", NVS_TYPE_BLOB},
    {"ml_peers", "tbl", NVS_TYPE_BLOB},          {"ml_config", "settings", NVS_TYPE_BLOB},
    {"ml_config", "peers", NVS_TYPE_BLOB},       {"ml_config", "wifi_list", NVS_TYPE_BLOB},
};
static const char* const spaces[] = {"microlink", "tailnet", "ml_wg", "ml_peers", "ml_config"};
void erase(void* data, size_t size) {
  volatile auto* bytes = static_cast<volatile uint8_t*>(data);
  while (size--) *bytes++ = 0;
}
struct Blob {
  uint8_t bytes[32]{};
  ~Blob() { erase(bytes, sizeof(bytes)); }
};
// Streaming SHA state stays below 256 bytes; no 192-byte identity copy, heap
// buffer or persistent duplicate of secret material. Wipe state on every exit.
class Digest {
#if defined(__APPLE__)
  CC_SHA256_CTX context{};
#elif defined(__linux__)
  SHA256_CTX context{};
#else
  mbedtls_sha256_context context{};
#endif
 public:
  Digest() {
#if defined(__APPLE__)
    CC_SHA256_Init(&context);
#elif defined(__linux__)
    SHA256_Init(&context);
#else
    mbedtls_sha256_init(&context);
#endif
  }
  ~Digest() {
#if !defined(__APPLE__) && !defined(__linux__)
    mbedtls_sha256_free(&context);
#endif
    erase(&context, sizeof(context));
  }
  bool start() {
#if defined(__APPLE__) || defined(__linux__)
    return true;
#else
    return mbedtls_sha256_starts(&context, 0) == 0;
#endif
  }
  bool add(const uint8_t* data, size_t size) {
#if defined(__APPLE__)
    return CC_SHA256_Update(&context, data, static_cast<CC_LONG>(size)) == 1;
#elif defined(__linux__)
    return SHA256_Update(&context, data, size) == 1;
#else
    return mbedtls_sha256_update(&context, data, size) == 0;
#endif
  }
  bool finish(uint8_t* output) {
#if defined(__APPLE__)
    return CC_SHA256_Final(output, &context) == 1;
#elif defined(__linux__)
    return SHA256_Final(output, &context) == 1;
#else
    return mbedtls_sha256_finish(&context, output) == 0;
#endif
  }
};
static_assert(sizeof(Digest) <= 256, "Keep the audit digest within the worker stack budget");
// Inventory precedes reads, making the digest independent of NVS iteration order.
// Bound iteration: NVS has finite space, but unknown future stores are not scanned
// without limit. Unrecognized records stay in place and block automatic reuse.
bool inventory(uint32_t& present, bool& unsupported, bool& corrupt) {
  for (const char* space : spaces) {
    nvs_iterator_t it = nullptr;
    auto error = nvs_entry_find("nvs", space, NVS_TYPE_ANY, &it);
    unsigned count = 0;
    while (error == ESP_OK) {
      nvs_entry_info_t entry{};
      error = nvs_entry_info(it, &entry);
      if (error != ESP_OK) break;
      bool known = false;
      for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        if (std::strcmp(fields[i].space, space) || std::strcmp(fields[i].key, entry.key)) continue;
        known = true;
        present |= uint32_t{1} << i;
        if (entry.type != fields[i].type) corrupt = true;
        break;
      }
      if (!known) unsupported = true;
      if (++count >= 64) {
        unsupported = true;
        nvs_release_iterator(it);
        return true;
      }
      error = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    if (error != ESP_ERR_NVS_NOT_FOUND) return false;
  }
  return true;
}
struct Handle {
  nvs_handle_t value;
  ~Handle() { nvs_close(value); }
};
bool readIdentity(uint32_t present, IdentityAudit& result, bool& corrupt) {
  if (!(present & 0x3f)) return true;
  nvs_handle_t value;
  if (nvs_open("microlink", NVS_READONLY, &value) != ESP_OK) return false;
  Handle handle{value};
  Digest digest;
  if (!digest.start()) return false;
  for (unsigned i = 0; i < 6; ++i) {
    if (!(present & (uint32_t{1} << i))) continue;
    size_t length = 0;
    if (nvs_get_blob(value, fields[i].key, nullptr, &length) != ESP_OK) return false;
    if (length != 32) {
      corrupt = true;
      continue;
    }
    Blob blob;
    if (nvs_get_blob(value, fields[i].key, blob.bytes, &length) != ESP_OK) return false;
    if (length != 32) {
      corrupt = true;
      continue;
    }
    if (!digest.add(blob.bytes, sizeof(blob.bytes))) return false;
    ++result.identityBlobs;
  }
  return digest.finish(result.fingerprint.data());
}
bool readFlags(uint32_t present, IdentityAudit& result, bool& corrupt, bool& unsupported) {
  if (!(present & 0x3fc0)) return true;
  nvs_handle_t value;
  if (nvs_open("tailnet", NVS_READONLY, &value) != ESP_OK) return false;
  Handle handle{value};
  for (unsigned i = 6; i < 14; ++i) {
    if (!(present & (uint32_t{1} << i))) continue;
    if (i < 8) {
      uint8_t flag = 0;
      if (nvs_get_u8(value, fields[i].key, &flag) != ESP_OK) return false;
      if (flag > 1) corrupt = true;
      if (i == 7) result.joined = flag == 1;
    } else if (i < 13) {
      uint32_t number = 0;
      if (nvs_get_u32(value, fields[i].key, &number) != ESP_OK) return false;
      if (i == 9 && number > 2) unsupported = true;
    }
    // login_url is volatile pairing presentation, not identity. Preserve it
    // opaquely, without allocating a URL buffer or exposing its contents.
  }
  return true;
}
}  // namespace
IdentityAudit auditIdentityImpl() {
  IdentityAudit result;
  uint32_t present = 0;
  bool unsupported = false, corrupt = false;
  if (!inventory(present, unsupported, corrupt)) return result;
  if (corrupt) {
    result.status = IdentityStatus::Corrupt;
    return result;
  }
  if (!readIdentity(present, result, corrupt) || !readFlags(present, result, corrupt, unsupported)) {
    result.fingerprint.fill(0);
    return result;
  }
  if (present & (uint32_t{1} << 14)) {
    nvs_handle_t value;
    if (nvs_open("ml_wg", NVS_READONLY, &value) != ESP_OK) {
      result.fingerprint.fill(0);
      return result;
    }
    Handle handle{value};
    uint64_t floor = 0;
    if (nvs_get_u64(value, "ts_floor", &floor) != ESP_OK) {
      result.fingerprint.fill(0);
      return result;
    }
  }
  if (corrupt)
    result.status = IdentityStatus::Corrupt;
  else if (unsupported)
    result.status = IdentityStatus::Unsupported;
  else if (result.identityBlobs == 6)
    result.status = IdentityStatus::RetainedInPlace;
  else if (result.identityBlobs || result.joined)
    result.status = IdentityStatus::Incomplete;
  else
    result.status = IdentityStatus::Absent;
  if (result.status != IdentityStatus::RetainedInPlace) result.fingerprint.fill(0);
  return result;
}
IdentityAudit auditIdentity() {
  auto result = auditIdentityImpl();
  if (result.status == IdentityStatus::ReadError)
    LOG_ERR("IMPORT", "Identity audit read failed; source retained");
  else if (result.status != IdentityStatus::RetainedInPlace && result.status != IdentityStatus::Absent)
    LOG_ERR("IMPORT", "Identity audit unresolved; source retained");
  return result;
}
}  // namespace booksync::import
// Full-link size evidence only; never invoked during boot before F29.
extern "C" __attribute__((used, retain)) void booksync_identity_audit_link(booksync::import::IdentityAudit* output) {
  if (output) *output = booksync::import::auditIdentity();
}
#endif
