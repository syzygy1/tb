/*
  Copyright (c) 2011-2013 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "defs.h"
#include "checksum.h"
#include "threads.h"
#include "citycrc.h"
#include "util.h"

#define CHUNK (1ULL << 24)

extern int total_work;

static uint64_t checksum1[2];
static uint64_t checksum2[2];
static uint64_t *results = NULL;
static uint8_t *data;
static uint64_t size;
static int checksum_found;
static int checksum_match;
static uint64_t *work = NULL;

static void checksum_worker(struct thread_data *thread)
{
  uint64_t idx;
  uint64_t end = thread->end;

  for (idx = thread->begin; idx < end; idx++) {
    uint64_t start = idx * CHUNK;
    uint64_t chunk = CHUNK;
    if (start + chunk > size)
      chunk = size - start;
    CityHashCrc256((char *)(data + start), chunk, &results[4 * idx]);
  }
}

static void calc_checksum(char *name)
{
  uint64_t orig_size;

  if (!work) work = alloc_work(total_work);

  data = map_file(name, 0, &size);
  orig_size = size;
  if ((size & 0x3f) == 0x10) {
    size &= ~0x3fULL;
    memcpy(checksum1, data + size, 16);
    checksum_found = 1;
  } else {
    if (size & 0x3f) {
      fprintf(stderr, "Size of %s is not a multiple of 64.\n", name);
      exit(1);
    }
    checksum_found = 0;
  }

  int chunks = (size + CHUNK - 1) / CHUNK;
  results = (uint64_t *)malloc(32 * chunks);
  fill_work(total_work, chunks, 0, work);
  run_threaded(checksum_worker, work, 0);
  CityHashCrc128((char *)results, 32 * chunks, checksum2);
  unmap_file(data, orig_size);
  free(results);

  if (checksum_found)
    checksum_match = (checksum1[0] == checksum2[0]
			&& checksum1[1] == checksum2[1]);
}

void print_checksum(char *name, char *sum)
{
  data = map_file(name, 1, &size);
  if ((size & 0x3f) == 0x10) {
    memcpy(checksum1, data + (size & ~0x3fULL), 16);
  } else {
    fprintf(stderr, "No checksum found.\n");
    exit(1);
  }
  unmap_file(data, size);

  int i;
  static char nibble[16] = "0123456789abcdef";
  uint8_t *c = (uint8_t *)checksum1;

  for (i = 0; i < 16; i++) {
    uint8_t b = c[i];
    sum[2 * i] = nibble[b >> 4];
    sum[2 * i + 1] = nibble[b & 0x0f];
  }
  sum[32] = 0;
}

void add_checksum(char *name)
{
  calc_checksum(name);
  if (checksum_found) {
    fprintf(stderr, "%s checksum already present.\n", checksum_match ? "Matching" : "Non-matching");
    exit(1);
  }
  FILE *F = fopen(name, "ab");
  if (!F) {
    fprintf(stderr, "Could not open %s for appending.\n", name);
    exit(1);
  }
  fwrite(checksum2, 16, 1, F);
  fclose(F);
}

void verify_checksum(char *name)
{
  printf("%s: ", name);
  calc_checksum(name);
  if (!checksum_found) {
    fprintf(stderr, "No checksum present.\n");
    exit(1);
  }
  if (!checksum_match)
    printf("FAIL!\n");
  else
    printf("OK!\n");
}

