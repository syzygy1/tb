/*
  Copyright (c) 2011-2013, 2018 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#include <stdalign.h>
#include <stdatomic.h>

#ifndef THREADS_H
#define THREADS_H
#include "defs.h"
#include "types.h"

#ifndef __WIN32__
#include <pthread.h>
#else
#include <windows.h>
#endif

#ifndef __WIN32__
#define LOCK_T pthread_mutex_t
#define LOCK_INIT(x) pthread_mutex_init(&(x), NULL)
#define LOCK(x) do { atomic_signal_fence(memory_order_seq_cst); pthread_mutex_lock(&(x)); } while (0)
#define UNLOCK(x) do { pthread_mutex_unlock(&(x)); atomic_signal_fence(memory_order_seq_cst); } while (0)
#else
#define LOCK_T HANDLE
#define LOCK_INIT(x) do { x = CreateMutex(NULL, FALSE, NULL); } while (0)
#define LOCK(x) do { atomic_signal_fence(memory_order_seq_cst); WaitForSingleObject(x, INFINITE); } while (0)
#define UNLOCK(x) do { ReleaseMutex(x); atomic_signal_fence(memory_order_seq_cst); } while (0)
#endif

struct thread_data {
  alignas(64) uint64_t begin;
  uint64_t end;
  bitboard occ;
  uint64_t *stats;
  int *p;
  int thread;
};

void init_threads(int pawns);
void run_threaded(void (*func)(struct thread_data *), uint64_t *work,
    int report_time);
void run_single(void (*func)(struct thread_data *), uint64_t *work,
    int report_time);
void fill_work(int n, uint64_t size, uint64_t mask, uint64_t *w);
void fill_work_offset(int n, uint64_t size, uint64_t mask, uint64_t *w,
    uint64_t offset);
uint64_t *alloc_work(int n);
uint64_t *create_work(int n, uint64_t size, uint64_t mask);

void create_compression_threads(void);
void run_compression(void (*func)(int t));

extern int numthreads;
extern int thread_affinity;
extern int total_work;
extern struct thread_data *thread_data;
extern struct timeval start_time, cur_time;

#endif
