// Native stats identity extracted from the retained GPL-3.0 KOReaderSyncClient.
#include <StatsIdentity.h>

#include <cstdint>
#include <cstdio>
#ifndef SIMULATOR
#include <esp_mac.h>
#endif
namespace StatsIdentity {
bool readerId(char* out, const size_t capacity) {
#ifdef SIMULATOR
  const int n = snprintf(out, capacity, "crossink-simulator");
#else
  uint8_t mac[6]{};
  if (esp_efuse_mac_get_default(mac) != ESP_OK) return false;
  const int n =
      snprintf(out, capacity, "crossink-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
#endif
  return n > 0 && static_cast<size_t>(n) < capacity;
}
}  // namespace StatsIdentity
