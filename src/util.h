#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>
#include <stdio.h>

void *map_file(char *name, int shared, uint64_t *size);
void unmap_file(void *data, uint64_t size);

void *alloc_aligned(uint64_t size, uintptr_t alignment);
void *alloc_huge(uint64_t size);

void write_u32(FILE *F, uint32_t v);
void write_u16(FILE *F, uint16_t v);
void write_u8(FILE *F, uint8_t v);

void write_bits(FILE *F, uint32_t bits, int n);

#define COPYSIZE 10*1024*1024
extern uint8_t *copybuf;

void copy_bytes(FILE *F, FILE *G, uint64_t num);
char *get_lz4_buf(void);

#endif
