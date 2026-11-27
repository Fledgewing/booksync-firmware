/*
 * MicroLink copies its config at microlink_init() and has no setter for the
 * priority peer. This reaches into the struct (microlink_internal.h, from the
 * pinned submodule, so a layout change fails the build) so the shim can pin a
 * peer on an existing handle between microlink_stop() and microlink_start(),
 * keeping the node's keys and session.
 */
#include "microlink_internal.h"
#include "wireguardif.h"
#include <stdio.h>

void tailnet_ml_set_priority_peer(microlink_t *ml, uint32_t vpn_ip) {
    if (ml) ml->config.priority_peer_ip = vpn_ip;
}

static void region_code(const microlink_t *ml, uint16_t id, char *out, size_t len) {
    out[0] = 0;
    if (!id) return;
    for (int i = 0; i < ml->derp_region_count; i++) {
        if (ml->derp_regions[i].region_id == id) {
            snprintf(out, len, "%s", ml->derp_regions[i].code);
            return;
        }
    }
    snprintf(out, len, "%u", (unsigned)id);
}

/* Region codes of our home relay and of a tracked peer's ("" if unknown). */
void tailnet_ml_relay_codes(const microlink_t *ml, uint32_t vpn_ip, char *home, char *peer, size_t len) {
    home[0] = peer[0] = 0;
    if (!ml) return;
    region_code(ml, ml->derp_home_region, home, len);
    for (int i = 0; vpn_ip && i < ML_MAX_PEERS; i++) {
        if (ml->peers[i].active && ml->peers[i].vpn_ip == vpn_ip) {
            region_code(ml, ml->peers[i].derp_region, peer, len);
            return;
        }
    }
}

/* Home relay region forced by the user (0 = closest, MicroLink probes). Takes
 * effect at the next microlink_start(). */
void tailnet_ml_set_forced_region(microlink_t *ml, uint16_t region) {
    if (ml) ml->derp_forced_region = region;
}

/* Our current home relay region (0 = not known yet). */
uint16_t tailnet_ml_home_region(const microlink_t *ml) { return ml ? ml->derp_home_region : 0; }

/* Home relay region of a tracked peer (0 = unknown). */
uint16_t tailnet_ml_peer_region(const microlink_t *ml, uint32_t vpn_ip) {
    for (int i = 0; ml && vpn_ip && i < ML_MAX_PEERS; i++) {
        if (ml->peers[i].active && ml->peers[i].vpn_ip == vpn_ip) return ml->peers[i].derp_region;
    }
    return 0;
}

/* Relay regions from the last DERPMap (owned by MicroLink's coord task; a
 * read racing a new map can show a stale entry, which is harmless here). */
int tailnet_ml_regions(const microlink_t *ml, uint16_t *ids, char (*codes)[8], int max) {
    int n = 0;
    for (int i = 0; ml && i < ml->derp_region_count && n < max; i++) {
        if (ml->derp_regions[i].avoid) continue;
        ids[n] = ml->derp_regions[i].region_id;
        snprintf(codes[n], 8, "%s", ml->derp_regions[i].code);
        n++;
    }
    return n;
}

/* WireGuard session to a peer. MicroLink's own microlink_tcp_connect() asks
 * wg_mgr for a handshake and waits for it; plain sockets (CrossInk's HTTP
 * clients) never ask, and its peers start passive, so a peer with no direct
 * path (relay only) never gets a session. The shim does what that API does. */
void tailnet_ml_request_handshake(microlink_t *ml, uint32_t vpn_ip) {
    if (ml) ml_wg_mgr_post_request(ml, ML_PEER_REQ_HANDSHAKE, vpn_ip);
}

bool tailnet_ml_tunnel_up(microlink_t *ml, uint32_t vpn_ip) { return ml && ml_wg_mgr_peer_is_up(ml, vpn_ip); }

/* The path to a peer for a log line, from WireGuard's own endpoint (where
 * its packets go now): "direct a.b.c.d:port" or "DERP region R". The rtt is
 * the last DISCO pong's when our side confirmed that direct path, else the
 * last handshake's initiation -> response time (which may mix DERP one way
 * and direct the other). */
void tailnet_ml_path(const microlink_t *ml, uint32_t vpn_ip, char *out, size_t len) {
    snprintf(out, len, "unknown");
    for (int i = 0; ml && vpn_ip && i < ML_MAX_PEERS; i++) {
        const ml_peer_t *p = &ml->peers[i];
        if (!p->active || p->vpn_ip != vpn_ip) continue;
        ip_addr_t ip;
        u16_t port = 0;
        bool direct = false;
        if (ml->wg_netif && p->wg_peer_index >= 0 &&
            wireguardif_peer_is_up((struct netif *)ml->wg_netif, (u8_t)p->wg_peer_index, &ip, &port) == ERR_OK &&
            !ip_addr_isany(&ip) && port != 0) {
            direct = true;
        }
        if (direct) {
            char ip_str[IPADDR_STRLEN_MAX];
            ipaddr_ntoa_r(&ip, ip_str, sizeof(ip_str));
            if (p->has_direct_path) {
                snprintf(out, len, "direct %s:%u, rtt %lu ms (DISCO)", ip_str, (unsigned)port,
                         (unsigned long)p->best_rtt_ms);
            } else {
                snprintf(out, len, "direct %s:%u, handshake rtt %lu ms (not confirmed by DISCO)", ip_str,
                         (unsigned)port, (unsigned long)wireguardif_last_handshake_rtt_ms());
            }
        } else {
            snprintf(out, len, "DERP region %u, handshake rtt %lu ms", (unsigned)p->derp_region,
                     (unsigned long)wireguardif_last_handshake_rtt_ms());
        }
        return;
    }
}
