# SPDX-License-Identifier: GPL-3.0-or-later
"""Owned BookSync Tailnet build. MicroLink is pinned in .gitmodules; no overlay/OTA/UI policy."""
import os
Import("env")
root = env.subst("$PROJECT_DIR")
here = os.path.join(root, "tailnet")
ml_dir = os.path.join(here, "third_party", "microlink", "components", "microlink")
wg_dir = os.path.join(ml_dir, "components", "wireguard_lwip", "src")
shim_dir = os.path.join(here, "shim")
config_h = os.path.join(here, "ml_config.h")
pioenv = env.subst("$PIOENV")
SIMULATOR = "simulator" in pioenv
has_psram = False if SIMULATOR else env.BoardConfig().get("build.mcu", "") == "esp32s3"
SWITCHES = {'CONFIG_ML_LITE': (1, None, None),
 'ML_LITE_TASKS': (1, 0, 0),
 'ML_LITE_CONTEXT': (1, 0, 0),
 'ML_LITE_NO_DIRECT': (2, 0, 0),
 'ML_LITE_SMALL_FILTER': (1, 0, 0),
 'ML_LITE_DERP_WOLFSSL': (1, 0, 0),
 'ML_LITE_CONTROL_PLAIN': (1, 0, 0),
 'ML_LITE_CONTROL_ONESHOT': (1, 0, 0),
 'ML_LITE_STREAM_MAP': (1, 0, 0),
 'ML_LITE_CACHED_START': (1, 0, 0),
 'ML_LITE_DERP_AFTER_CONTROL': (1, 0, 0),
 'ML_LITE_REG_ONEBUF': (1, 1, 1),
 'ML_LITE_DERP_FRAME_CAP': (4096, 4096, 4096),
 'ML_LITE_DERP_LAZY': (1, 0, 0),
 'ML_LITE_DERP_TRIM': (1, 0, 0),
 'ML_LITE_RX_ONEBUF': (1, 0, 0),
 'ML_LITE_SMALL_QUEUES': (1, 0, 0),
 'ML_LITE_NACL_CACHE_SLOTS': (2, 0, 0),
 'ML_WG_REKEY_AFTER_S': (0, 0, 0),
 'ML_DERP_RATE_LOG': (1, 1, 1),
 'ML_LITE_MAX_PEERS': (1, None, None),
 'ML_TASK_TUNNEL_STACK': (5120, None, None),
 'CONFIG_ML_MAX_PEERS': (8, None, None),
 'CONFIG_ML_H2_BUFFER_SIZE_KB': (48, None, None),
 'CONFIG_ML_JSON_BUFFER_SIZE_KB': (48, None, None),
 'TAILNET_STACK_LOG': (0, 0, 0),
 'TAILNET_TASK_STACK_WRAP': (0, 0, 0),
 'TAILNET_STACK_ESP_TIMER': (0, 0, 0),
 'TAILNET_STACK_TCPIP': (0, 0, 0),
 'TAILNET_STACK_ARDUINO_EVENTS': (0, 0, 0),
 'TAILNET_STACK_SYS_EVT': (0, 0, 0),
 'TAILNET_STACK_WIFI': (0, 0, 0),
 'TAILNET_STACK_TMR_SVC': (0, 0, 0),
 'TAILNET_LAZY_WORKER': (1, 1, 1),
 'TAILNET_WORKER_STACK': (4096, 6144, 6144),
 'TAILNET_LOW_MEMORY_HOLD_MS': (5000, 0, 0),
 'TAILNET_WIFI_STATIC_RX': (2, 0, 0),
 'TAILNET_WIFI_DYNAMIC_RX': (4, 0, 0),
 'TAILNET_WIFI_DYNAMIC_TX': (4, 0, 0),
 'TAILNET_WIFI_RX_MGMT': (3, 0, 0),
 'TAILNET_WIFI_PARKED_STATIC_RX': (3, 0, 0),
 'TAILNET_WIFI_PARKED_DYNAMIC_RX': (8, 0, 0),
 'TAILNET_WIFI_PARKED_DYNAMIC_TX': (8, 0, 0),
 'TAILNET_WIFI_PARKED_RX_MGMT': (5, 0, 0),
 'TAILNET_WIFI_NO_NVS': (1, 1, 1),
 'TAILNET_WIFI_ALLOC_COUNT': (0, 0, 0)}
SWITCH_VALUES = {name: row[1 if has_psram else 0] for name, row in SWITCHES.items()}
ML_DEFINES = [(name, value) for name, value in SWITCH_VALUES.items()
              if value is not None and name.startswith(("ML_", "CONFIG_ML_"))]
if not SIMULATOR:
    env.Append(CPPDEFINES=[(name, value) for name, value in SWITCH_VALUES.items()
                          if value is not None and not name.startswith(("ML_", "CONFIG_ML_"))])
derp_wolfssl = not has_psram and not SIMULATOR
if derp_wolfssl:
    env.Append(CPPDEFINES=["WOLFSSL_SHA384", "WOLFSSL_SHA512", "WOLFSSL_SP_384"])
ML_SRCS = [
    "microlink.c", "ml_coord.c", "ml_coord_tls.c", "ml_map_stream.c", "ml_derp.c", "ml_net_io.c",
    "ml_wg_mgr.c", "ml_stun.c", "ml_noise.c", "ml_h2.c", "ml_udp.c", "ml_filter.c",
    "ml_routes.c", "ml_nat66.c", "ml_ota.c", "ml_tcp.c", "ml_peer_nvs.c",
    "ml_zerocopy.c", "ml_cellular.c", "ml_at_socket.c", "ml_at_validate.c",
    "ml_net_switch.c", "ml_config_httpd.c",
]
WG_SRCS = ["wireguard.c", "wireguardif.c", "wireguard-filter.c", "crypto.c", "wireguard-platform-esp32.c"]
WG_CRYPTO = ["blake2s.c", "chacha20.c", "chacha20poly1305.c", "poly1305-donna.c", "x25519.c"]

# Framework include paths and flags are only set inside BuildProgram()
# (ProcessProgramDeps -> BuildFrameworks), after pre-scripts run. Hook the next
# step, ProcessProjectDeps, so the clones below inherit the full environment and
# our objects still reach the program's link.
_orig_process_project_deps = env.ProcessProjectDeps  # noqa: F821


def _process_project_deps_with_tailnet(env):
    _orig_process_project_deps()
    if os.path.basename(env.subst("$PROJECT_SRC_DIR")) == ".dummy":
        # The framework's lib-builder pass (custom_sdkconfig) links a dummy sketch to
        # rebuild the ESP-IDF libraries. It has none of CrossInk's libraries (CrossInk v2:
        # not even Arduino-wolfSSL, which MicroLink's DERP TLS needs), and nothing of
        # ours belongs in it: no MicroLink, no shim, no link wraps.
        print("Tailnet build: framework lib-builder pass, MicroLink and the shim left out")
        return
    ml_env = env.Clone()
    ml_env.Append(
        CPPPATH=[os.path.join(ml_dir, "include"), os.path.join(ml_dir, "src"), wg_dir,
                 os.path.join(wg_dir, "crypto", "refc")],
        # The framework compiles logs below ERROR out; MicroLink and the shim
        # keep INFO so TailnetLog.cpp can show them on the Tailscale screen.
        CPPDEFINES=["WIREGUARD_CRYPTO_REFC", ("LOG_LOCAL_LEVEL", 3)],
        CCFLAGS=["-include", config_h, "-Wno-stringop-overread"],
    )
    # Per-device switches (SWITCHES above). The peer table stays small
    # (ml_config.h: 16; 8 on the C3): the reader only needs its servers, and
    # tailnet_ensure_peer() pins one that a large tailnet would otherwise leave out.
    ml_env.Append(CPPDEFINES=ML_DEFINES)

    if derp_wolfssl:
        # ml_derp.c includes wolfSSL; the defines (WOLFSSL_USER_SETTINGS, ...)
        # come with the cloned environment, the headers from its library.
        wolfssl_src = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), pioenv, "Arduino-wolfSSL", "src")
        if not os.path.isfile(os.path.join(wolfssl_src, "wolfssl", "ssl.h")):
            raise RuntimeError(f"wolfSSL not found at {wolfssl_src}; the DERP relay TLS needs Arduino-wolfSSL")
        ml_env.Append(CPPPATH=[wolfssl_src])

    build_dir = os.path.join(env.subst("$BUILD_DIR"), "tailnet")
    objs = []
    for name in ML_SRCS:
        objs.append(ml_env.Object(os.path.join(build_dir, "ml", name.replace(".c", ".o")),
                                  os.path.join(ml_dir, "src", name)))
    for name in WG_SRCS:
        objs.append(ml_env.Object(os.path.join(build_dir, "wg", name.replace(".c", ".o")),
                                  os.path.join(wg_dir, name)))
    hot_env = ml_env.Clone()
    hot_env.Append(CCFLAGS=["-O2"])  # WireGuard + DISCO crypto, as MicroLink's CMake does
    for name in WG_CRYPTO:
        objs.append(hot_env.Object(os.path.join(build_dir, "wg", name.replace(".c", ".o")),
                                   os.path.join(wg_dir, "crypto", "refc", name)))
    objs.append(hot_env.Object(os.path.join(build_dir, "ml", "nacl_box.o"),
                               os.path.join(ml_dir, "src", "nacl_box.c")))

    microlink_lib = ml_env.StaticLibrary(os.path.join(build_dir, "microlink"), objs)

    # --- 2. The shim, linked directly ----------------------------------------------

    shim_env = ml_env.Clone()
    # Arduino library headers reach only the library builders that the LDF
    # picked; the shim needs WiFi (and Network, which WiFi.h includes).
    fw_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
    shim_env.Append(CPPPATH=[os.path.join(fw_dir, "libraries", lib, "src") for lib in ("WiFi", "Network")])
    shim_objs = [
        shim_env.Object(os.path.join(build_dir, "shim", os.path.splitext(f)[0] + ".o"), os.path.join(shim_dir, f))
        for f in sorted(os.listdir(shim_dir)) if f.endswith((".cpp", ".c"))
    ]
    env.Append(PIOBUILDFILES=shim_objs)
    env.Append(LIBS=[microlink_lib])
    env.Append(LINKFLAGS=["-Wl,--wrap=lwip_getaddrinfo", "-Wl,--wrap=lwip_connect",
                          # TailnetLog.cpp: keep our log lines past the
                          # prebuilt ERROR-only log level
                          "-Wl,--wrap=esp_log",
                          # TailnetTlsMem.c: mbedTLS buffers in PSRAM
                          "-Wl,--wrap=esp_mbedtls_mem_calloc",
                          # TailnetWifiMem.c: the WiFi init switches
                          "-Wl,--wrap=esp_wifi_init",
                          # TailnetStacks.c: MicroLink stops before WiFi goes off
                          "-Wl,--wrap=esp_wifi_stop",
                          # TailnetLwipLock.c: Arduino's hostByName clears the DNS cache
                          # without the lwIP core lock (a panic beside MicroLink's lookups)
                          "-Wl,--wrap=dns_clear_cache",
                          # TailnetLwipLock.c: no new socket before the TCP/IP stack exists
                          # (Wi-Fi never started: the core lock is NULL and lwIP asserts)
                          "-Wl,--wrap=lwip_socket"])
    if SWITCH_VALUES.get("TAILNET_TASK_STACK_WRAP"):
        # TailnetStacks.c: task stack sizes by name, and the timer task's. CrossInk 2.x
        # wraps xTaskCreatePinnedToCore itself; its wrapper calls the shim's hooks (CrossInk
        # patch 0020) instead of a second wrapper.
        env.Append(LINKFLAGS=["-Wl,--wrap=vApplicationGetTimerTaskMemory"])


if not SIMULATOR:
    env.AddMethod(_process_project_deps_with_tailnet, "ProcessProjectDeps")
# Host simulation links the same neutral admission policy, with fake transport in tests.
else:
    original = env.ProcessProjectDeps
    def with_admission(env):
        original()
        env.Append(PIOBUILDFILES=[env.Object(os.path.join(env.subst("$BUILD_DIR"), "tailnet-admission.o"),
                                          os.path.join(shim_dir, "TailnetNetPhase.cpp"))])
    env.AddMethod(with_admission, "ProcessProjectDeps")
env.Append(CPPPATH=[shim_dir])
