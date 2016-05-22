/*
  Copyright (c) 2011-2013 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#ifndef THREADS_H
#define THREADS_H
#include "defs.h"

#ifndef __WIN32__
#include <pthread.h>
#else
#include <windows.h>
#endif

#ifndef __WIN32__
#define LOCK_T pthread_mutex_t
#define LOCK_INIT(x) pthread_mutex_init(&(x), NULL)
#define LOCK(x) pthread_mutex_lock(&(x))
#define UNLOCK(x) pthread_mutex_unlock(&(x))
#else
#define LOCK_T HANDLE
#define LOCK_INIT(x) do { x = CreateMutex(NULL, FALSE, NULL); } while (0)
#define LOCK(x) WaitForSingleObject(x, INFINITE)
#define UNLOCK(x) ReleaseMutex(x)
#endif

#ifndef MAX_THREADS
#define MAX_THREADS 12
#endif

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
