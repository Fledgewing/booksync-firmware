// SPDX-License-Identifier: GPL-3.0-or-later
// TLS data prefers PSRAM on capable boards. Stacks always remain internal.
#include <esp_heap_caps.h>
#include <stdint.h>
#include <stddef.h>
#include "Tailnet.h"
void *__wrap_esp_mbedtls_mem_calloc(size_t n, size_t size) {
    if (!n || !size) return NULL;
    if (n > SIZE_MAX / size) {
        tailnet_log_emit("E tailnet: TLS allocation size overflow\n");
        return NULL;
    }
    const unsigned internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
#if FREEINK_FB_PSRAM
    // TLS owns and frees these buffers with heap_caps_free. No PSRAM stack.
    void *p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) return p;
#endif
    const size_t bytes = n * size;
    if (bytes > SIZE_MAX - 12 * 1024 ||
        heap_caps_get_free_size(internal) < bytes + 12 * 1024 ||
        heap_caps_get_largest_free_block(internal) < bytes + 8 * 1024) {
        tailnet_log_emit("E tailnet: TLS internal reserve refused %u bytes\n", (unsigned)bytes);
        return NULL;
    }
    void *result = heap_caps_calloc(n, size, internal);
    if (!result) tailnet_log_emit("E tailnet: TLS allocation failed: %u bytes\n", (unsigned)bytes);
    return result;
}
