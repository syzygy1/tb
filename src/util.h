#ifndef UTIL_H
#define UTIL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifdef _WIN32
typedef HANDLE map_t;
typedef HANDLE FD;
#define FD_ERR INVALID_HANDLE_VALUE
#define SEP_CHAR ';'

#else
typedef size_t map_t;
typedef int FD;
#define FD_ERR -1
#define SEP_CHAR ':'

#endif

#undef min
#define min(a,b) ((a) < (b) ? (a) : (b))

FD open_file(const char *name);
void close_file(FD fd);

size_t file_size(FD fd);

void *map_file(int fd, bool shared, map_t *map);
void unmap_file(void *data, map_t map);

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
