// mbedTLS allocations in PSRAM.
//
// The prebuilt framework has CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC, so every
// mbedTLS buffer comes from internal RAM. MicroLink holds two TLS sessions
// (control server and relay), and with its task stacks that took the X4 Pro
// from ~180 KB of free internal RAM to ~33 KB, below what library sync
// requires (it refused: "Insufficient heap for sync request"). The link wraps
// esp_mbedtls_mem_calloc (tailnet_build.py) to prefer PSRAM for anything but
// small allocations, as CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC would. Boards
// without PSRAM fall back to internal RAM, as before.
#include <esp_heap_caps.h>
#include <stddef.h>

#define TAILNET_TLS_PSRAM_MIN 512

void *__wrap_esp_mbedtls_mem_calloc(size_t n, size_t size) {
    const unsigned internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    if (n * size < TAILNET_TLS_PSRAM_MIN) return heap_caps_calloc(n, size, internal);
    return heap_caps_calloc_prefer(n, size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, internal);
}
