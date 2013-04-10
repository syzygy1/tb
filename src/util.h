#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>

char *map_file(char *name, int shared, long64 *size);
void unmap_file(char *data, long64 size);

ubyte *alloc_aligned(long64 size, uintptr_t alignment);
ubyte *alloc_huge(long64 size);

#endif

