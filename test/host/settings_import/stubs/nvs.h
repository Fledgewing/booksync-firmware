#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
using nvs_handle_t = unsigned;
constexpr int ESP_OK = 0, ESP_ERR_NVS_NOT_FOUND = 1, ESP_FAIL = 2, NVS_READONLY = 0;
inline struct FakeNvs {
  std::vector<uint8_t> blob;
  bool missingNamespace = false, missingKey = false, failed = false;
  unsigned opens = 0, closes = 0;
  std::string name, key;
  int mode = -1;
} fakeNvs;
inline int nvs_open(const char* name, int mode, nvs_handle_t* handle) {
  ++fakeNvs.opens;
  fakeNvs.name = name;
  fakeNvs.mode = mode;
  *handle = 1;
  return fakeNvs.missingNamespace ? ESP_ERR_NVS_NOT_FOUND : fakeNvs.failed ? ESP_FAIL : ESP_OK;
}
inline int nvs_get_blob(nvs_handle_t, const char* key, void* data, size_t* size) {
  fakeNvs.key = key;
  if (fakeNvs.missingKey) return ESP_ERR_NVS_NOT_FOUND;
  if (fakeNvs.failed) return ESP_FAIL;
  if (data && *size < fakeNvs.blob.size()) return ESP_FAIL;
  *size = fakeNvs.blob.size();
  if (data) memcpy(data, fakeNvs.blob.data(), *size);
  return ESP_OK;
}
inline void nvs_close(nvs_handle_t) { ++fakeNvs.closes; }
// Write/erase/commit functions are deliberately absent: any new mutation fails to link.
