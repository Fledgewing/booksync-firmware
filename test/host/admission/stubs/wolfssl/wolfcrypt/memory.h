#pragma once
#include <stddef.h>
int wolfSSL_SetAllocators(void* (*allocate)(size_t), void (*release)(void*), void* (*resize)(void*, size_t));
