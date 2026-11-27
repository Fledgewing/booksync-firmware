// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
using esp_err_t = int;
using nvs_handle_t = unsigned;
constexpr int ESP_OK = 0, ESP_ERR_NVS_NOT_FOUND = 1, ESP_FAIL = 2;
constexpr int NVS_READONLY = 0;
enum nvs_type_t { NVS_TYPE_U8, NVS_TYPE_U32, NVS_TYPE_U64, NVS_TYPE_STR, NVS_TYPE_BLOB, NVS_TYPE_ANY };
struct Iterator;
using nvs_iterator_t = Iterator*;
struct nvs_entry_info_t {
  char namespace_name[16];
  char key[16];
  nvs_type_t type;
};
esp_err_t nvs_open(const char*, int, nvs_handle_t*);
void nvs_close(nvs_handle_t);
esp_err_t nvs_entry_find(const char*, const char*, nvs_type_t, nvs_iterator_t*);
esp_err_t nvs_entry_next(nvs_iterator_t*);
esp_err_t nvs_entry_info(nvs_iterator_t, nvs_entry_info_t*);
void nvs_release_iterator(nvs_iterator_t);
esp_err_t nvs_get_blob(nvs_handle_t, const char*, void*, size_t*);
esp_err_t nvs_get_u8(nvs_handle_t, const char*, uint8_t*);
esp_err_t nvs_get_u32(nvs_handle_t, const char*, uint32_t*);
esp_err_t nvs_get_u64(nvs_handle_t, const char*, uint64_t*);
