/*
 * Kconfig values for MicroLink.
 *
 * MicroLink is an ESP-IDF component and reads CONFIG_ML_* from sdkconfig.h.
 * CrossInk builds against pioarduino's prebuilt ESP-IDF libraries (the X4 Pro
 * deliberately keeps them intact), so there is no menuconfig step: these
 * defines stand in for it. tailnet_build.py force-includes this header into
 * every MicroLink and shim translation unit.
 *
 * Booleans are defined to 1 or left undefined, as sdkconfig.h does.
 */
#pragma once

/* Peers and tunnels tracked. A reader only needs its servers. */
#ifndef CONFIG_ML_MAX_PEERS
#define CONFIG_ML_MAX_PEERS 16
#endif
/* Peers cached in NVS, 92 bytes each. The readers' NVS partition is 20 KB and
 * is shared with CrossInk and the WiFi driver. */
#define CONFIG_ML_NVS_MAX_PEERS 16
#define CONFIG_ML_PRIORITY_PEER_IP ""

/* DERP home region before the first latency probe; MicroLink then picks the
 * closest region itself (patches/microlink). */
#define CONFIG_ML_DERP_REGION 9
/* Relay connections to other peers' home regions, besides our own. Off: each
 * is a TLS session in internal RAM (the prebuilt mbedTLS can't use PSRAM), and
 * on the X4 Pro a third session next to CrossInk's own fails to allocate
 * (-0x7f00, seen on a device). Peers running tailscaled reach the reader
 * through its home region anyway, and Relay region > Match server (the
 * default) puts both ends in one region. */
#define CONFIG_ML_DERP_EXTRA_CONNS 0
/* The prebuilt Arduino mbedTLS has the certificate bundle; see
 * tailnet_build.py, which checks CONFIG_MBEDTLS_CERTIFICATE_BUNDLE. */
#define CONFIG_ML_DERP_VERIFY_CERT 1

/* Map-response buffers. They go to PSRAM when the board has it; the C3 has
 * none, so its build lowers these (see tailnet_build.py). */
#ifndef CONFIG_ML_H2_BUFFER_SIZE_KB
#define CONFIG_ML_H2_BUFFER_SIZE_KB 512
#endif
#ifndef CONFIG_ML_JSON_BUFFER_SIZE_KB
#define CONFIG_ML_JSON_BUFFER_SIZE_KB 512
#endif

#define CONFIG_ML_FILTER_MAX_ENTRIES 4096
#define CONFIG_ML_UDP_RX_TASK_PRIO 7
#define CONFIG_ML_STOP_TIMEOUT_MS 30000
#define CONFIG_ML_ADVERTISE_ROUTES ""
#define CONFIG_ML_CONFIG_MAX_ALLOWED_PEERS 16

/* Control plane: hosted Tailscale, interactive login (no key in the image). */
#define CONFIG_ML_CTRL_HOST "controlplane.tailscale.com"
#define CONFIG_ML_CTRL_NOISE_PUBKEY_HEX ""
#define CONFIG_ML_CTRL_TLS_VERIFY_CERT 1
#define CONFIG_ML_TAILSCALE_AUTH_KEY ""
#define CONFIG_ML_DEVICE_NAME ""
#define CONFIG_ML_WIFI_SSID ""
#define CONFIG_ML_WIFI_PASSWORD ""

/* Off: CrossInk owns OTA, the web UI and the radio. MicroLink's own OTA
 * rollback, config web server, cellular, failover and router stay out. */
#define CONFIG_ML_OTA_CONFIRM_TIMEOUT_S 300
#define CONFIG_ML_CONFIG_HTTPD_PASSWORD ""
#define CONFIG_ML_CELLULAR_APN ""
#define CONFIG_ML_CELLULAR_SIM_PIN ""
#define CONFIG_ML_CELLULAR_PPP_USER ""
#define CONFIG_ML_CELLULAR_PPP_PASS ""
#define CONFIG_ML_CELLULAR_REG_TIMEOUT_S 180
#define CONFIG_ML_CELLULAR_FAST_BAUD 0
#define CONFIG_ML_NET_SWITCH_WIFI_TIMEOUT_MS 30000
#define CONFIG_ML_NET_SWITCH_HEALTH_INTERVAL_MS 30000
#define CONFIG_ML_NET_SWITCH_FAILBACK_MS 120000
