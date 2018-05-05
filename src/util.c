#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#ifndef __WIN32__
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#else
#include <windows.h>
#endif

#include "lz4.h"

#include "defs.h"
#include "util.h"

void *map_file(char *name, int shared, uint64_t *size)
{
#ifndef __WIN32__

  struct stat statbuf;
  int fd = open(name, O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "Could not open %s for reading.\n", name);
    exit(1);
  }
  fstat(fd, &statbuf);
  *size = statbuf.st_size;
#ifdef __linux__
  void *data = mmap(NULL, statbuf.st_size, PROT_READ,
                    shared ? MAP_SHARED : MAP_PRIVATE | MAP_POPULATE, fd, 0);
#else
  void *data = mmap(NULL, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);
#endif
  if (data == MAP_FAILED) {
    printf("Could not mmap() %s.\n", name);
    exit(1);
  }
  close(fd);
  return data;

#else

  HANDLE h = CreateFile(name, GENERIC_READ, FILE_SHARE_READ, NULL,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "Could not open %s for reading.\n", name);
    exit(1);
  }
  DWORD size_low, size_high;
  size_low = GetFileSize(h, &size_high);
  *size = ((uint64_t)size_high << 32) | (uint64_t)size_low;
  HANDLE map = CreateFileMapping(h, NULL, PAGE_READONLY, size_high, size_low,
                                 NULL);
  if (map == NULL) {
    fprintf(stderr, "CreateFileMapping() failed.\n");
    exit(1);
  }
  void *data = MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0);
  if (data == NULL) {
    fprintf(stderr, "MapViewOfFile() failed.\n");
    exit(1);
  }
  CloseHandle(h);
  return data;

#endif
}

void unmap_file(void *data, uint64_t size)
{
#ifndef __WIN32__

  munmap(data, size);

#else

  UnmapViewOfFile(data);

#endif
}

void *alloc_aligned(uint64_t size, uintptr_t alignment)
{
#ifndef __WIN32__

  void *ptr;

  posix_memalign(&ptr, alignment, size);
  if (ptr == NULL) {
    fprintf(stderr, "Could not allocate sufficient memory.\n");
    exit(1);
  }

  return ptr;

#else

  void *ptr;

  ptr = malloc(size + alignment - 1);
  if (ptr == NULL) {
    fprintf(stderr, "Could not allocate sufficient memory.\n");
    exit(1);
  }
  ptr = (void *)((uintptr_t)(ptr + alignment - 1) & ~(alignment - 1));

  return ptr;

#endif
}

void *alloc_huge(uint64_t size)
{
  void *ptr;

#ifndef __WIN32__

  posix_memalign(&ptr, 2 * 1024 * 1024, size);
  if (ptr == NULL) {
    fprintf(stderr, "Could not allocate sufficient memory.\n");
    exit(1);
  }
#ifdef MADV_HUGEPAGE
  madvise(ptr, size, MADV_HUGEPAGE);
#endif

#else

  ptr = malloc(size);
  if (ptr == NULL) {
    fprintf(stderr, "Could not allocate sufficient memory.\n");
    exit(1);
  }

#endif

  return ptr;
}

void write_u32(FILE *F, uint32_t v)
{
  fputc(v & 0xff, F);
  fputc((v >> 8) & 0xff, F);
  fputc((v >> 16) & 0xff, F);
  fputc((v >> 24) & 0xff, F);
}

void write_u16(FILE *F, uint16_t v)
{
  fputc(v & 0xff, F);
  fputc((v >> 8) & 0xff, F);
}

void write_u8(FILE *F, uint8_t v)
{
  fputc(v, F);
}

static uint8_t buf[8192];

void write_bits(FILE *F, uint32_t bits, int n)
{
  static int numBytes, numBits;

  if (n > 0) {
    if (numBits) {
      if (numBits >= n) {
	buf[numBytes - 1] |= (bits << (numBits - n));
	numBits -= n;
	n = 0;
      } else {
	buf[numBytes - 1] |= (bits >> (n - numBits));
	n -= numBits;
	numBits = 0;
      }
    }
    while (n >= 8) {
      buf[numBytes++] = bits >> (n - 8);
      n -= 8;
    }
    if (n > 0) {
      buf[numBytes++] = bits << (8 - n);
      numBits = 8 - n;
    }
  } else if (n == 0) {
    numBytes = 0;
    numBits = 0;
  } else if (n < 0) {
    n = -n;
    while (numBytes < n)
      buf[numBytes++] = 0;
    fwrite(buf, 1, n, F);
    numBytes = 0;
    numBits = 0;
  }
}

#define COPYSIZE (10*1024*1024)

static uint8_t *copybuf = NULL;
static char *lz4_buf = NULL;

static void init(void)
{
  if (!copybuf) {
    copybuf = malloc(COPYSIZE);
    lz4_buf = malloc(8 + LZ4_compressBound(COPYSIZE));
  }
}

static void file_read(void *ptr, size_t size, FILE *F)
{
  if (fread(ptr, 1, size, F) != size) {
    fprintf(stderr, "Error reading data from disk.\n");
    exit(EXIT_FAILURE);
  }
}

static void file_write(void *ptr, size_t size, FILE *F)
{
  if (fwrite(ptr, 1, size, F) != size) {
    fprintf(stderr, "Error writing data to disk.\n");
    exit(EXIT_FAILURE);
  }
}

void copy_data(FILE *F, FILE *G, uint64_t size)
{
  init();

  while (size) {
    uint32_t chunk = min(COPYSIZE, size);
    file_read(copybuf, chunk, G);
    file_write(copybuf, chunk, F);
    size -= chunk;
  }
}

void write_data(FILE *F, uint8_t *src, uint64_t size)
{
  init();

  while (size) {
    uint32_t chunk = min(COPYSIZE, size);
    uint32_t lz4_size = LZ4_compress((char *)src, lz4_buf + 4, chunk);
    *(uint32_t *)lz4_buf = lz4_size;
    file_write(lz4_buf, lz4_size + 4, F);
    src += chunk;
    size -= chunk;
  }
}

void write_mapped_data(FILE *F, uint8_t *src, uint64_t size, uint8_t *v)
{
  init();

  while (size) {
    uint32_t chunk = min(COPYSIZE, size);
    for (size_t i = 0; i < chunk; i++)
      copybuf[i] = v[src[i]];
    uint32_t lz4_size = LZ4_compress((char *)copybuf, lz4_buf + 4, chunk);
    *(uint32_t *)lz4_buf = lz4_size;
    file_write(lz4_buf, lz4_size + 4, F);
    src += chunk;
    size -= chunk;
  }
}

void write_mapped_data_p(FILE *F, uint8_t *src, uint64_t size, uint8_t *v)
{
  init();

  while (size) {
    uint32_t chunk = min(COPYSIZE, size);
    for (size_t i = 0; i < chunk; i++)
      copybuf[i] = v[src[i]];
    uint32_t lz4_size = LZ4_compress((char *)copybuf, lz4_buf + 8, chunk);
    ((uint32_t *)lz4_buf)[0] = lz4_size;
    ((uint32_t *)lz4_buf)[1] = chunk;
    file_write(lz4_buf, lz4_size + 8, F);
    src += chunk;
    size -= chunk;
  }
}

void read_data_u8(FILE *F, uint8_t *dst, uint64_t size)
{
  init();

  while (size) {
    uint32_t chunk = min(COPYSIZE, size);
    uint32_t lz4_size;
    file_read(&lz4_size, 4, F);
    file_read(lz4_buf, lz4_size, F);
    LZ4_uncompress(lz4_buf, (char *)dst, chunk);
    dst += chunk;
    size -= chunk;
  }
}

void read_data_u16(FILE *F, uint16_t *dst, uint64_t size)
{
  init();

  while (size) {
    uint32_t chunk = min(COPYSIZE, size);
    uint32_t lz4_size;
    file_read(&lz4_size, 4, F);
    file_read(lz4_buf, lz4_size, F);
    LZ4_uncompress(lz4_buf, (char *)copybuf, chunk);
    for (size_t i = 0; i < chunk; i++)
      dst[i] = copybuf[i];
    dst += chunk;
    size -= chunk;
  }
}

void read_mapped_data_u8(FILE *F, uint8_t *dst, uint64_t size, uint8_t *v)
{
  init();

  while (size) {
    uint32_t chunk = min(COPYSIZE, size);
    uint32_t lz4_size;
    file_read(&lz4_size, 4, F);
    file_read(lz4_buf, lz4_size, F);
    LZ4_uncompress(lz4_buf, (char *)copybuf, chunk);
    for (size_t i = 0; i < chunk; i++)
      dst[i] |= v[copybuf[i]];
    dst += chunk;
    size -= chunk;
  }
}

void read_mapped_data_u16(FILE *F, uint16_t *dst, uint64_t size, uint16_t *v)
{
  init();

  while (size) {
    uint32_t chunk = min(COPYSIZE, size);
    uint32_t lz4_size;
    file_read(&lz4_size, 4, F);
    file_read(lz4_buf, lz4_size, F);
    LZ4_uncompress(lz4_buf, (char *)copybuf, chunk);
    for (size_t i = 0; i < chunk; i++)
      dst[i] |= v[copybuf[i]];
    dst += chunk;
    size -= chunk;
  }
}

void read_mapped_data_p_u8(FILE *F, uint8_t *dst, uint64_t size, uint8_t *v)
{
  init();

  while (size) {
    uint32_t lz4_size, chunk;
    file_read(&lz4_size, 4, F);
    file_read(&chunk, 4, F);
    file_read(lz4_buf, lz4_size, F);
    LZ4_uncompress(lz4_buf, (char *)copybuf, chunk);
    for (size_t i = 0; i < chunk; i++)
      dst[i] |= v[copybuf[i]];
    dst += chunk;
    size -= chunk;
  }
}
