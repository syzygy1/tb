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
#include <sys/mman.h>
#include <fcntl.h>

#include "defs.h"
#include "checksum.h"
#include "threads.h"
#include "citycrc.h"

#define CHUNK (1ULL << 24)

extern int total_work;

uint64 checksum1[2];
uint64 checksum2[2];
uint64 *results;
char *data;
long64 size;
int checksum_found;
int checksum_match;

static void checksum_worker(struct thread_data *thread)
{
  long64 idx;
  long64 end = thread->end;

  for (idx = thread->begin; idx < end; idx++) {
    long64 start = idx * CHUNK;
    long64 chunk = CHUNK;
    if (start + chunk > size)
      chunk = size - start;
    CityHashCrc256(data + start, chunk, &results[4 * idx]);
  }
}

static void calc_checksum(char *name)
{
  struct stat statbuf;
  int fd = open(name, O_RDONLY);
  if (fd < 0) {
    printf("Could not open %s for reading.\n", name);
    exit(1);
  }
  fstat(fd, &statbuf);
  size = statbuf.st_size;
  data = (char *)mmap(NULL, size, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
  close(fd);
  if ((size & 0x3f) == 0x10) {
    size &= ~0x3fULL;
    memcpy(checksum1, data + size, 16);
    checksum_found = 1;
  } else {
    if (size & 0x3f) {
      printf("Size of %s is not a multiple of 64.\n", name);
      exit(1);
    }
    checksum_found = 0;
  }

  int chunks = (size + CHUNK - 1) / CHUNK;
  results = (uint64 *)malloc(32 * chunks);
  long64 *work = create_work(total_work, chunks, 0);
  run_threaded(checksum_worker, work, 0);
  munmap(data, statbuf.st_size);
  free(work);

  CityHashCrc128((char *)results, 32 * chunks, checksum2);

  if (checksum_found)
    checksum_match = (checksum1[0] == checksum2[0]
			&& checksum1[1] == checksum2[1]);
}

void print_checksum(char *name)
{
  struct stat statbuf;

  printf("%s: ", name);
  int fd = open(name, O_RDONLY);
  if (fd < 0) {
    printf("Could not open %s for reading.\n", name);
    exit(1);
  }
  fstat(fd, &statbuf);
  size = statbuf.st_size;
  data = (char *)mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if ((size & 0x3f) == 0x10) {
    size &= ~0x3fULL;
    memcpy(checksum1, data + size, 16);
  } else {
    printf("No checksum found.\n");
    exit(1);
  }
  munmap(data, statbuf.st_size);

  int i;
  static char nibble[16] = "0123456789abcdef";
  ubyte *c = (ubyte *)checksum1;

  for (i = 0; i < 16; i++) {
    ubyte b = c[i];
    putc(nibble[b >> 4], stdout);
    putc(nibble[b & 0x0f], stdout);
  }
  putc('\n', stdout);
}

void add_checksum(char *name)
{
  calc_checksum(name);
  if (checksum_found) {
    printf("%s checksum already present.\n", checksum_match ? "Matching" : "Non-matching");
    exit(1);
  }
  int fd = open(name, O_WRONLY | O_APPEND);
  if (fd < 0) {
    printf("Could not open %s for appending.\n", name);
    exit(1);
  }
  if (write(fd, checksum2, 16) < 16) {
    perror("write");
    exit(1);
  }
  close(fd);
}

void verify_checksum(char *name)
{
  printf("%s: ", name);
  calc_checksum(name);
  if (!checksum_found) {
    printf("No checksum present.\n");
    exit(1);
  }
  if (!checksum_match) {
    printf("FAIL!\n");
  }
  printf("OK!\n");
}

