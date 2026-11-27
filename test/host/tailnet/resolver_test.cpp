// SPDX-License-Identifier: GPL-3.0-or-later
#include <cassert>
#include <cerrno>
#include <cstring>
#include <string>
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "Tailnet.h"
extern "C" int __wrap_lwip_getaddrinfo(const char*, const char*, const addrinfo*, addrinfo**);
extern "C" int __wrap_lwip_connect(int, const sockaddr*, socklen_t);
static bool started = true, ready = true, peer = true, enabled = true;
static int waits = 0, demands = 0, ensures = 0, connects = 0;
static std::string resolved;
extern "C" bool tailnet_lwip_started() { return started; }
extern "C" bool tailnet_enabled() { return enabled; }
extern "C" void tailnet_log_emit(const char*, ...) {}
extern "C" uint32_t tailnet_resolve(const char* name) {
  return ready && strcmp(name, "peer") == 0 ? 0x64400001u : 0;
}
extern "C" bool tailnet_start_on_demand(const char*) { ++demands; return true; }
extern "C" bool tailnet_wait_connected_for(const char*, uint32_t ms) { assert(ms == 20000); ++waits; return ready; }
extern "C" bool tailnet_ensure_peer(uint32_t, uint32_t ms) { assert(ms == 20000); ++ensures; return peer; }
extern "C" int __real_lwip_getaddrinfo(const char* name, const char*, const addrinfo*, addrinfo**) {
  resolved = name ? name : ""; return 27;
}
extern "C" int __real_lwip_connect(int, const sockaddr*, socklen_t) { ++connects; return 31; }
int main() {
  addrinfo hints{};
  auto lookup = [&](const char* name) { return __wrap_lwip_getaddrinfo(name, "443", &hints, nullptr); };
  started = false;
  assert(lookup("peer") == EAI_FAIL && demands == 0);
  started = true;
  assert(lookup("peer") == 27 && waits == 0 && resolved != "peer");
  ready = false;
  lookup("peer"); assert(waits == 1 && demands == 1 && resolved == "peer");
  lookup("PEER.tail.ts.net."); assert(waits == 2);
  lookup("public.example.invalid"); assert(waits == 2);
  hints.ai_flags = AI_NUMERICHOST; lookup("peer"); assert(waits == 2);
  hints.ai_flags = 0; hints.ai_family = AF_INET6; lookup("peer"); assert(waits == 2);
  hints.ai_family = AF_INET; lookup(nullptr); assert(waits == 2);
  sockaddr_in address{}; address.sin_family = AF_INET;
  assert(__wrap_lwip_connect(1, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1);
  assert(errno == EHOSTUNREACH && connects == 0);
  address.sin_addr.s_addr = htonl(0x64400001u); peer = false;
  assert(__wrap_lwip_connect(1, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1 && ensures == 1);
  peer = true;
  assert(__wrap_lwip_connect(1, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 31 && connects == 1);
  enabled = false; peer = false;
  assert(__wrap_lwip_connect(1, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 31 && ensures == 2);
}
