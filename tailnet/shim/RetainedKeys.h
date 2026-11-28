// SPDX-License-Identifier: GPL-3.0-or-later
// Included by the pinned MicroLink compilation only, after microlink_t.
// Strict reuse: every actual load rechecks all six reads. No generation, public
// key derivation, write, commit, or ephemeral fallback is available here.
#pragma once
static esp_err_t booksync_load_retained_keys(microlink_t* ml) {
  static const char* const keys[] = {"machine_pri", "machine_pub", "wg_private", "wg_public", "disco_pri", "disco_pub"};
  uint8_t* const destinations[] = {ml->machine_private_key, ml->machine_public_key, ml->wg_private_key,
                                   ml->wg_public_key,       ml->disco_private_key,  ml->disco_public_key};
  nvs_handle_t handle;
  esp_err_t result = nvs_open("microlink", NVS_READONLY, &handle);
  if (result == ESP_OK) {
    for (unsigned i = 0; i < 6; ++i) {
      size_t bytes = 32;
      result = nvs_get_blob(handle, keys[i], destinations[i], &bytes);
      if (result != ESP_OK || bytes != 32) {
        result = ESP_FAIL;
        break;
      }
    }
    nvs_close(handle);
  }
  if (result != ESP_OK) {
    for (unsigned i = 0; i < 6; ++i) {
      volatile uint8_t* data = destinations[i];
      for (unsigned b = 0; b < 32; ++b) data[b] = 0;
    }
  }
  return result;
}
