#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>
#include <stdio.h>

#define min(a,b) ((a) < (b) ? (a) : (b))

void *map_file(char *name, int shared, uint64_t *size);
void unmap_file(void *data, uint64_t size);

void *alloc_aligned(uint64_t size, uintptr_t alignment);
void *alloc_huge(uint64_t size);

void write_u32(FILE *F, uint32_t v);
void write_u16(FILE *F, uint16_t v);
void write_u8(FILE *F, uint8_t v);

void write_bits(FILE *F, uint32_t bits, int n);

void copy_data(FILE *F, FILE *G, uint64_t num);
void write_data(FILE *F, uint8_t *src, uint64_t offset, uint64_t size,
    uint8_t *v);
void read_data_u8(FILE *F, uint8_t *dst, uint64_t size, uint8_t *v);
void read_data_u16(FILE *F, uint16_t *dst, uint64_t size, uint16_t *v);

#endif
