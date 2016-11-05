#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>

void *map_file(char *name, int shared, long64 *size);
void unmap_file(void *data, long64 size);

void *alloc_aligned(long64 size, uintptr_t alignment);
void *alloc_huge(long64 size);

#endif

