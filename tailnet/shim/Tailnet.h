// Tailnet: Tailscale (via MicroLink) layered onto CrossInk without editing it.
//
// The service starts MicroLink whenever the reader's WiFi station gets an IP
// address and stops it when WiFi goes away, so every screen that turns WiFi on
// (library sync, catalog, OPDS, OTA, web server) gets the tailnet for free.
// It installs itself from a static constructor; CrossInk calls nothing.
//
// Worker-only transport API. Product UI consumes Library values and intents.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  TAILNET_OFF = 0,       // WiFi is down, or the service is disabled
  TAILNET_STARTING,      // contacting the control plane
  TAILNET_NEEDS_LOGIN,   // tailnet_login_url() has the URL to show
  TAILNET_CONNECTED,     // has a 100.x address; tailnet hosts are reachable
  TAILNET_RECONNECTING,
  TAILNET_FAILED,        // auth rejected or a hard error; see the serial log
} tailnet_status_t;

tailnet_status_t tailnet_status(void);

// Buffer size that always holds a login URL (MicroLink's MICROLINK_AUTH_URL_MAX).
#define TAILNET_LOGIN_URL_MAX 256

// Copies the interactive login URL into buf. Returns false when there is none
// (status is not TAILNET_NEEDS_LOGIN).
bool tailnet_login_url(char* buf, size_t len);

// The reader's tailnet IPv4 address in host byte order, or 0.
uint32_t tailnet_ip(void);

// Bumped on every status change, so a screen can redraw only when needed.
uint32_t tailnet_generation(void);

// Persisted on/off switch (NVS), off by default. Off stops MicroLink and keeps
// it stopped; on starts it whenever WiFi is up.
bool tailnet_enabled(void);
void tailnet_set_enabled(bool enabled);

// True once this reader has connected to a tailnet, until tailnet_logout().
bool tailnet_joined(void);

// Forget this reader's tailnet identity (keys and cached peers). The next
// start asks for a fresh login.
void tailnet_logout(void);

// Block until the tailnet is connected, while it is starting or reconnecting.
// Returns false at once when it is off, failed or waiting for a login.
bool tailnet_wait_connected(uint32_t timeout_ms);
// The same, logging "<what> waiting for Tailscale (up to N ms)" and the outcome.
// Never waits on MicroLink's own tasks, lwIP's or the Tailscale worker.
bool tailnet_wait_connected_for(const char* what, uint32_t timeout_ms);

// Make sure MicroLink tracks the peer at ip (host byte order). On a tailnet
// larger than its peer table, a peer can be left out; this pins it as the
// priority peer (remembered in NVS) and restarts MicroLink so it is added
// (with one-shot control, the C3 build: runs a control session for a fresh
// map instead, and runs one more if the peer's tunnel does not come up).
// Tried once per peer per WiFi session. Blocks up to about 2 x timeout_ms.
bool tailnet_ensure_peer(uint32_t ip, uint32_t timeout_ms);

// Diagnostics for the Tailscale screen.
typedef struct {
  int peer_count;      // peers MicroLink tracks now
  int peer_capacity;   // size of its peer table (full = the reader sees too much)
  uint32_t pin_ip;     // pinned (priority) peer, 0 if none
  uint32_t target_ip;  // last tailnet address the reader connected to (else the pin)
  bool target_known;   // ...is in MicroLink's peer table
  bool target_online;  // ...is online according to the control plane
  bool target_direct;  // ...is reached directly (else via a DERP relay)
  char home_relay[8];    // our home relay region code (closest), e.g. "sea"
  char target_relay[8];  // the server's home relay region code
} tailnet_diag_t;
void tailnet_get_diag(tailnet_diag_t* out);

// Home relay (DERP) region; MATCH_SERVER by default. AUTO: the closest, measured at each start.
// MATCH_SERVER: the home region of the library server (the tailnet address
// the reader last connected to, or the pinned one), learned and remembered.
// FIXED: a region the user picked. Persisted; applying it restarts MicroLink.
typedef enum { TAILNET_RELAY_AUTO = 0, TAILNET_RELAY_MATCH_SERVER = 1, TAILNET_RELAY_FIXED = 2 } tailnet_relay_mode_t;
tailnet_relay_mode_t tailnet_relay_mode(void);
uint16_t tailnet_relay_fixed_region(void);
void tailnet_set_relay(tailnet_relay_mode_t mode, uint16_t region);
// Regions from the last relay map (empty until first connected); codes like "sea".
int tailnet_relay_regions(uint16_t* ids, char (*codes)[8], int max);
// Region code for an id ("" if unknown); our current home region code.
void tailnet_relay_code(uint16_t id, char* out, size_t len);

// Small-memory (lite) build: the ESP32-C3 readers, no PSRAM. MicroLink and the
// display's framebuffer do not fit together there, so MicroLink starts only in
// the boots that free the framebuffer for it (TailnetNetPhase.h), which call
// tailnet_permit_start(); elsewhere WiFi works without Tailscale. Login is its
// own boot: register_only makes MicroLink only register (no map, no relay).
bool tailnet_lite(void);
void tailnet_permit_start(bool register_only);
// Lite: this boot starts Tailscale only once a request needs it: a tailnet
// name to resolve or a connection to a tailnet address (100.64.0.0/10, also
// what a router's DNS may answer for a server name). The resolver and connect
// hooks call tailnet_start_on_demand(); true if it started Tailscale.
void tailnet_permit_on_demand(void);
bool tailnet_start_on_demand(const char* why);
// Login boot: waits until the register-only session has an answer:
// TAILNET_NEEDS_LOGIN (tailnet_login_url() has the URL), TAILNET_CONNECTED
// (logged in), TAILNET_FAILED (not accepted, or stopped for low memory), or
// anything else after timeout_ms (no answer).
tailnet_status_t tailnet_login_session(uint32_t timeout_ms);
// Lite builds: the login URL the last login session got (kept in NVS until
// the reader is logged in), for the QR once the network is off again.
bool tailnet_saved_login_url(char* buf, size_t len);
// Called once WiFi has joined, before the first request (TailnetNetPhase.h's
// tailnet_wifi_joined()): in a lite build's parked network boot, blocks until
// Tailscale can carry traffic: control session done and closed (its memory
// back), library server in the peer table, WireGuard tunnel to it up. At most
// timeout_ms. Returns at once elsewhere. False if Tailscale did not connect.
bool tailnet_wait_ready(uint32_t timeout_ms);
// Before turning WiFi off: waits (up to timeout_ms) for a running control
// session to finish.
void tailnet_wait_idle(uint32_t timeout_ms);
// Before WiFi goes off (the esp_wifi_stop link wrap, TailnetStacks.c): stops MicroLink and
// waits up to timeout_ms for its tasks to exit. No-op when it is not running, and on
// MicroLink's, the worker's and the event tasks.
void tailnet_stop_before_wifi_off(uint32_t timeout_ms);
// Catalog probe (c3-catalog-probe): start Tailscale in this boot without
// parking the framebuffer; one log line of heap numbers for a named step.
void tailnet_probe_permit_unparked(void);
void tailnet_probe_heap(const char* step);
// A request's heap window: begin (free and largest before), mark at a phase
// boundary (the lowest point since the last call), end (the last phase).
void tailnet_probe_window_begin(const char* what);
void tailnet_probe_window_mark(const char* what, const char* phase);
void tailnet_probe_window_end(const char* what, const char* phase);
// TCP connects made so far by the task that first called this (the catalog's request
// task): a catalog request that made none reused its kept connection.
uint32_t tailnet_connect_count(void);
// One log line: the path traffic to the server takes now (direct or DERP)
// and its measured round trip, prefixed with `what`.
void tailnet_log_path(const char* what);

// Recent MicroLink and shim log lines, newest first (index 0). False past the
// end. Captured in RAM because the framework's console log is mostly off.
bool tailnet_log_line(int newest_index, char* buf, size_t len);
uint32_t tailnet_log_seq(void);
// Log a shim line (printf format, with its own level prefix and newline).
void tailnet_log_emit(const char* format, ...) __attribute__((format(printf, 1, 2)));  // changes whenever a line is added
void tailnet_log_install(void);  // called once at startup by the shim

// Resolve a tailnet name ("peer", "peer.<tail>.ts.net") to an IPv4
// address in host byte order via MicroLink's peer list. 0 if unknown.
uint32_t tailnet_resolve(const char* name);

#ifdef __cplusplus
}
#endif
