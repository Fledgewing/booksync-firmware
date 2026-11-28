#pragma once
#include <sys/time.h>
inline void (*clockSyncCallback)(timeval*) = nullptr;
inline bool clockSntpEnabled = false;
inline int clockSntpStatus = 0;
inline bool esp_sntp_enabled() { return clockSntpEnabled; }
inline void sntp_set_time_sync_notification_cb(void (*cb)(timeval*)) { clockSyncCallback = cb; }
constexpr int SNTP_SYNC_STATUS_COMPLETED = 1;
inline int sntp_get_sync_status() { return clockSntpStatus; }
