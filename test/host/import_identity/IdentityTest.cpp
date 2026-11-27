// SPDX-License-Identifier: GPL-3.0-or-later
#include <nvs.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "booksync/import/Identity.h"
struct Entry {
  std::string ns, key;
  nvs_type_t type;
  std::vector<uint8_t> bytes;
  bool operator==(const Entry&) const = default;
};
static std::vector<Entry> entries;
static int opens, closes, failAt = -1, calls;
static bool failed() { return calls++ == failAt; }
struct Iterator {
  std::vector<Entry> values;
  size_t at = 0;
};
static int iterators;
esp_err_t nvs_open(const char* ns, int mode, nvs_handle_t* h) {
  assert(mode == NVS_READONLY);
  if (failed()) return ESP_FAIL;
  static const char* spaces[] = {"microlink", "tailnet", "ml_wg", "ml_peers", "ml_config"};
  for (unsigned i = 0; i < 5; ++i)
    if (ns == std::string(spaces[i])) {
      *h = i + 1;
      ++opens;
      return ESP_OK;
    }
  return ESP_ERR_NVS_NOT_FOUND;
}
void nvs_close(nvs_handle_t) { ++closes; }
esp_err_t nvs_entry_find(const char* partition, const char* ns, nvs_type_t, nvs_iterator_t* it) {
  assert(std::string(partition) == "nvs");
  *it = nullptr;
  if (failed()) return ESP_FAIL;
  std::vector<Entry> matches;
  for (const auto& e : entries)
    if (e.ns == ns) matches.push_back(e);
  if (matches.empty()) return ESP_ERR_NVS_NOT_FOUND;
  *it = new Iterator{matches};
  ++iterators;
  return ESP_OK;
}
esp_err_t nvs_entry_next(nvs_iterator_t* it) {
  if (failed()) return ESP_FAIL;
  if (++(*it)->at < (*it)->values.size()) return ESP_OK;
  nvs_release_iterator(*it);
  *it = nullptr;
  return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_entry_info(nvs_iterator_t it, nvs_entry_info_t* info) {
  if (failed()) return ESP_FAIL;
  const auto& e = it->values[it->at];
  std::strcpy(info->namespace_name, e.ns.c_str());
  std::strcpy(info->key, e.key.c_str());
  info->type = e.type;
  return ESP_OK;
}
void nvs_release_iterator(nvs_iterator_t it) {
  if (it) {
    delete it;
    --iterators;
  }
}
static const Entry* lookup(nvs_handle_t h, const char* key) {
  static const char* spaces[] = {"microlink", "tailnet", "ml_wg", "ml_peers", "ml_config"};
  for (const auto& e : entries)
    if (e.ns == spaces[h - 1] && e.key == key) return &e;
  return nullptr;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char* key, void* dst, size_t* length) {
  if (failed()) return ESP_FAIL;
  auto e = lookup(h, key);
  if (!e) return ESP_ERR_NVS_NOT_FOUND;
  if (dst) {
    if (*length < e->bytes.size()) return ESP_FAIL;
    std::memcpy(dst, e->bytes.data(), e->bytes.size());
  }
  *length = e->bytes.size();
  return ESP_OK;
}
template <class T>
static esp_err_t scalar(nvs_handle_t h, const char* key, T* value) {
  if (failed()) return ESP_FAIL;
  auto e = lookup(h, key);
  if (!e || e->bytes.size() != sizeof(T)) return ESP_FAIL;
  *value = 0;
  for (size_t i = 0; i < sizeof(T); ++i) *value |= T(e->bytes[i]) << (8 * i);
  return ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t h, const char* k, uint8_t* v) { return scalar(h, k, v); }
esp_err_t nvs_get_u32(nvs_handle_t h, const char* k, uint32_t* v) { return scalar(h, k, v); }
esp_err_t nvs_get_u64(nvs_handle_t h, const char* k, uint64_t* v) { return scalar(h, k, v); }
static booksync::import::IdentityAudit audit() {
  opens = closes = calls = 0;
  auto before = entries;
  auto result = booksync::import::auditIdentity();
  assert(entries == before && opens == closes && iterators == 0);
  const int firstCalls = calls;
  if (failAt < 0) {
    auto again = booksync::import::auditIdentity();
    assert(result.status == again.status && result.fingerprint == again.fingerprint);
    assert(result.identityBlobs == again.identityBlobs && result.joined == again.joined);
    assert(entries == before && opens == closes && iterators == 0);
  }
  calls = firstCalls;
  return result;
}
int main(int argc, char** argv) {
  assert(argc == 2);
  std::ifstream input(argv[1]);
  std::string ns, key, type, hex;
  while (input >> ns >> key >> type >> hex) {
    Entry e{ns, key, NVS_TYPE_BLOB, {}};
    if (type == "u8") e.type = NVS_TYPE_U8;
    if (type == "u32") e.type = NVS_TYPE_U32;
    if (type == "u64") e.type = NVS_TYPE_U64;
    if (type == "str") e.type = NVS_TYPE_STR;
    if (hex != "-")
      for (size_t i = 0; i < hex.size(); i += 2) e.bytes.push_back(std::stoul(hex.substr(i, 2), nullptr, 16));
    entries.push_back(e);
  }
  assert(entries.size() == 15);
  using booksync::import::IdentityStatus;
  auto original = entries;
  auto first = audit(), second = audit();
  assert(first.status == IdentityStatus::RetainedInPlace && first.identityBlobs == 6 && first.joined);
  assert(first.fingerprint == second.fingerprint);
  const int successfulCalls = calls;
  for (int i = 0; i < successfulCalls; ++i) {
    failAt = i;
    assert(audit().status == IdentityStatus::ReadError);
  }
  failAt = -1;
  for (size_t i = 0; i < 6; ++i) {
    entries = original;
    entries.erase(entries.begin() + 8 + i);
    assert(audit().status == IdentityStatus::Incomplete);
    entries = original;
    entries[8 + i].bytes.pop_back();
    assert(audit().status == IdentityStatus::Corrupt);
    entries = original;
    entries[8 + i].type = NVS_TYPE_STR;
    assert(audit().status == IdentityStatus::Corrupt);
  }
  entries = original;
  std::reverse(entries.begin(), entries.end());
  assert(audit().fingerprint == first.fingerprint);
  entries = original;
  entries[8].bytes[0] ^= 1;
  assert(audit().fingerprint != first.fingerprint);
  entries = original;
  entries.push_back({"microlink", "future", NVS_TYPE_BLOB, {0, 1}});
  assert(audit().status == IdentityStatus::Unsupported);
  entries = original;
  entries[0].bytes = {2};
  assert(audit().status == IdentityStatus::Corrupt);
  entries = original;
  entries[3].bytes = {99, 0, 0, 0};
  assert(audit().status == IdentityStatus::Unsupported);
  entries = original;
  entries.push_back({"ml_config", "settings", NVS_TYPE_BLOB, {1, 2, 3}});
  assert(audit().status == IdentityStatus::RetainedInPlace);
  entries = original;
  entries.erase(entries.begin() + 8, entries.begin() + 14);
  assert(audit().status == IdentityStatus::Incomplete);
  entries.clear();
  assert(audit().status == IdentityStatus::Absent);
  entries = original;
  entries.back().type = NVS_TYPE_BLOB;
  assert(audit().status == IdentityStatus::Corrupt);
  entries = original;
  entries.push_back({"ml_peers", "future", NVS_TYPE_BLOB, {1}});
  assert(audit().status == IdentityStatus::Unsupported);
  entries = original;
  for (unsigned i = 0; i < 70; ++i) entries.push_back({"ml_config", "old" + std::to_string(i), NVS_TYPE_BLOB, {1}});
  assert(audit().status == IdentityStatus::Unsupported);
  entries = original;
  entries.erase(entries.begin(), entries.begin() + 8);
  assert(audit().status == IdentityStatus::RetainedInPlace);
  std::cout << "F26c: two boots, source retention, unknown/corrupt/old, and " << successfulCalls
            << " read-fault cuts passed\n";
}
