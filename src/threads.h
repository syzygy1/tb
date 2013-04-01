/*
  Copyright (c) 2011-2013 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#ifndef THREADS_H
#define THREADS_H
#include "defs.h"

#define MAX_THREADS 12

struct thread_data {
  long64 begin;
  long64 end;
  bitboard occ;
  long64 *stats;
  int *p;
  int thread;
  char dummy[64 - 2*sizeof(long64) - 2*sizeof(void *) - sizeof(bitboard) - sizeof(int)];
};

void init_threads(int pawns);
void run_threaded(void (*func)(struct thread_data *), long64 *work, int report_time);
void run_single(void (*func)(struct thread_data *), long64 *work, int report_time);
void fill_work(int n, long64 size, long64 mask, long64 *w);
long64 *alloc_work(int n);
long64 *create_work(int n, long64 size, long64 mask);

#endif
