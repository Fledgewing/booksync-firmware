// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SIMULATOR
#include <esp_rom_sys.h>
#include <nvs_flash.h>

extern "C" esp_err_t __real_nvs_flash_init();
// Arduino calls this before setup/initVariant. Do not let its two special
// errors authorize formatting an unsupported or unreadable legacy partition.
// Leave the underlying initialization failure intact as an unavailable store.
extern "C" esp_err_t __wrap_nvs_flash_init() {
  const auto error = __real_nvs_flash_init();
  if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    esp_rom_printf("BookSync: NVS unavailable; partition retained without formatting\n");
    return ESP_FAIL;
  }
  return error;
}
#endif
