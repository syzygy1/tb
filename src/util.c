#include <stdatomic.h>
#include <stddef.h>
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

#ifdef USE_ZSTD
#include <zstd.h>
#else
#include "lz4.h"
#endif

#include "defs.h"
#include "threads.h"
#include "util.h"

void *map_file(char *name, int shared, uint64_t *size)
{
#ifndef __WIN32__

  struct stat statbuf;
  int fd = open(name, O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "Could not open %s for reading.\n", name);
    exit(EXIT_FAILURE);
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
    fprintf(stderr, "Could not mmap() %s.\n", name);
    exit(EXIT_FAILURE);
  }
  close(fd);
  return data;

#else

  HANDLE h = CreateFile(name, GENERIC_READ, FILE_SHARE_READ, NULL,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "Could not open %s for reading.\n", name);
    exit(EXIT_FAILURE);
  }
  DWORD size_low, size_high;
  size_low = GetFileSize(h, &size_high);
  *size = ((uint64_t)size_high << 32) | (uint64_t)size_low;
  HANDLE map = CreateFileMapping(h, NULL, PAGE_READONLY, size_high, size_low,
                                 NULL);
  if (map == NULL) {
    fprintf(stderr, "CreateFileMapping() failed.\n");
    exit(EXIT_FAILURE);
  }
  void *data = MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0);
  if (data == NULL) {
    fprintf(stderr, "MapViewOfFile() failed.\n");
    exit(EXIT_FAILURE);
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
    exit(EXIT_FAILURE);
  }

  return ptr;

#else

  void *ptr;

  ptr = malloc(size + alignment - 1);
  if (ptr == NULL) {
    fprintf(stderr, "Could not allocate sufficient memory.\n");
    exit(EXIT_FAILURE);
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
    exit(EXIT_FAILURE);
  }
#ifdef MADV_HUGEPAGE
  madvise(ptr, size, MADV_HUGEPAGE);
#endif

#else

  ptr = malloc(size);
  if (ptr == NULL) {
    fprintf(stderr, "Could not allocate sufficient memory.\n");
    exit(EXIT_FAILURE);
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

static size_t compress_bound;

static LOCK_T cmprs_mutex;

static FILE *cmprs_F;
static void *cmprs_ptr;
static volatile size_t cmprs_size;
static void *cmprs_v;
static size_t cmprs_idx;

struct CompressFrame {
  uint32_t cmprs_chunk;
  uint32_t chunk;
  size_t idx;
  uint8_t data[];
};

#define HEADER_SIZE offsetof(struct CompressFrame, data)

struct CompressState {
  uint8_t *buffer;
  struct CompressFrame *frame;
#ifdef USE_ZSTD
  ZSTD_CCtx *c_ctx;
  ZSTD_DCtx *d_ctx;
#endif
};

static struct CompressState cmprs_state[COMPRESSION_THREADS];

static void init(void)
{
  static int initialised = 0;

  if (!initialised) {
    initialised = 1;
    LOCK_INIT(cmprs_mutex);
#ifdef USE_ZSTD
    compress_bound = ZSTD_compressBound(COPYSIZE);
#else
    compress_bound = LZ4_compressBound(COPYSIZE);
#endif
    for (int i = 0; i < COMPRESSION_THREADS; i++) {
      cmprs_state[i].buffer = malloc(COPYSIZE);
      cmprs_state[i].frame = malloc(HEADER_SIZE + compress_bound);
#ifdef USE_ZSTD
      cmprs_state[i].c_ctx = ZSTD_createCCtx();
      cmprs_state[i].d_ctx = ZSTD_createDCtx();
#endif
    }
    create_compression_threads();
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

#ifdef USE_ZSTD

static size_t compress(struct CompressState *state, void *dst, void *src,
    size_t chunk)
{
  return ZSTD_compressCCtx(state->c_ctx, dst, compress_bound, src, chunk, 1);
}

static void decompress(struct CompressState *state, void *dst, size_t chunk,
    void *src, size_t compressed)
{
  ZSTD_decompressDCtx(state->d_ctx, dst, chunk, src, compressed);
}

#else

static size_t compress(struct CompressState *state, void *dst, void *src,
    size_t chunk)
{
  (void)state;
  return LZ4_compress(src, dst, chunk);
}

static void decompress(struct CompressState *state, void *dst, size_t chunk,
    void *src, size_t compressed)
{
  (void)state;
  (void)compressed;
  LZ4_uncompress(src, dst, chunk);
}

#endif

void copy_data(FILE *F, FILE *G, uint64_t size)
{
  init();

  uint8_t *buffer = cmprs_state[0].buffer;

  while (size) {
    uint32_t chunk = min(COPYSIZE, size);
    file_read(buffer, chunk, G);
    file_write(buffer, chunk, F);
    size -= chunk;
  }
}

static void write_data_worker(int t)
{
  struct CompressState *state = &cmprs_state[t];

  FILE *F = cmprs_F;
  uint8_t *src = cmprs_ptr;
  uint8_t *v = cmprs_v;
  while (1) {
    LOCK(cmprs_mutex);
    size_t idx = cmprs_idx;
    uint32_t chunk = min(COPYSIZE, cmprs_size - idx);
    cmprs_idx += chunk;
    UNLOCK(cmprs_mutex);
    if (chunk == 0)
      break;
    uint8_t *buf;
    if (v) {
      for (size_t i = 0; i < chunk; i++)
        state->buffer[i] = v[src[idx + i]];
      buf = state->buffer;
    } else
      buf = src + idx;
    uint32_t cmprs_chunk = compress(state, state->frame->data, buf, chunk);
    state->frame->cmprs_chunk = cmprs_chunk;
    state->frame->chunk = chunk;
    state->frame->idx = idx;
    file_write(state->frame, cmprs_chunk + HEADER_SIZE, F);
  }
}

void write_data(FILE *F, uint8_t *src, uint64_t offset, uint64_t size,
    uint8_t *v)
{
  init();

  cmprs_F = F;
  cmprs_ptr = src;
  cmprs_size = offset + size;
  cmprs_v = v;
  cmprs_idx = offset;
  run_compression(write_data_worker);
}

static void read_data_worker_u8(int t)
{
  struct CompressState *state = &cmprs_state[t];

  FILE *F = cmprs_F;
  uint8_t *dst = cmprs_ptr;
  uint8_t *v = cmprs_v;
  while (1) {
    uint32_t cmprs_chunk;
    flockfile(F);
    if (cmprs_size == 0) {
      funlockfile(F);
      break;
    }
    file_read(&cmprs_chunk, 4, F);
    file_read(&state->frame->chunk, cmprs_chunk + HEADER_SIZE - 4, F);
    uint32_t chunk = state->frame->chunk;
    if (chunk > cmprs_size) {
      fprintf(stderr, "Error in read_data_worker.\n");
      exit(EXIT_FAILURE);
    }
    cmprs_size -= chunk;
    funlockfile(F);
    size_t idx = state->frame->idx;
    if (!v)
      decompress(state, dst + idx, chunk, state->frame->data, cmprs_chunk);
    else {
      decompress(state, state->buffer, chunk, state->frame->data, cmprs_chunk);
      for (size_t i = 0; i < chunk; i++)
        dst[idx + i] |= v[state->buffer[i]];
    }
  }
}

void read_data_u8(FILE *F, uint8_t *dst, uint64_t size, uint8_t *v)
{
  init();

  cmprs_F = F;
  cmprs_ptr = dst;
  cmprs_size = size;
  cmprs_v = v;
  run_compression(read_data_worker_u8);
}

static void read_data_worker_u16(int t)
{
  struct CompressState *state = &cmprs_state[t];

  FILE *F = cmprs_F;
  uint16_t *dst = cmprs_ptr;
  uint16_t *v = cmprs_v;
  while (1) {
    uint32_t cmprs_chunk;
    flockfile(F);
    if (cmprs_size == 0) {
      funlockfile(F);
      break;
    }
    file_read(&cmprs_chunk, 4, F);
    file_read(&state->frame->chunk, cmprs_chunk + HEADER_SIZE - 4, F);
    uint32_t chunk = state->frame->chunk;
    if (chunk > cmprs_size) {
      fprintf(stderr, "Error in read_data_worker.\n");
      exit(EXIT_FAILURE);
    }
    cmprs_size -= chunk;
    funlockfile(F);
    size_t idx = state->frame->idx;
    decompress(state, state->buffer, chunk, state->frame->data, cmprs_chunk);
    if (!v)
      for (size_t i = 0; i < chunk; i++)
        dst[idx + i] = state->buffer[i];
    else
      for (size_t i = 0; i < chunk; i++)
        dst[idx + i] |= v[state->buffer[i]];
  }
}

// Read 8-bit data into 16-bit array.
void read_data_u16(FILE *F, uint16_t *dst, uint64_t size, uint16_t *v)
{
  init();

  cmprs_F = F;
  cmprs_ptr = dst;
  cmprs_size = size;
  cmprs_v = v;
  run_compression(read_data_worker_u16);
}
