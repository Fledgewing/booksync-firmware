#pragma once
#include <stddef.h>
#define MALLOC_CAP_INTERNAL 1u
#define MALLOC_CAP_SPIRAM 2u
#define MALLOC_CAP_8BIT 4u
#ifdef __cplusplus
extern "C" {
#endif
void* heap_caps_malloc(size_t, unsigned);
void* heap_caps_calloc(size_t, size_t, unsigned);
void heap_caps_free(void*);
size_t heap_caps_get_allocated_size(void*);
size_t heap_caps_get_free_size(unsigned);
size_t heap_caps_get_largest_free_block(unsigned);
#ifdef __cplusplus
}
#endif
