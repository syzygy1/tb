/*
  Copyright (c) 2011-2013, 2018 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#include <stdalign.h>

#ifndef THREADS_H
#define THREADS_H

#include <stdalign.h>

#ifndef __WIN32__
#include <pthread.h>
#else
#include <windows.h>
#endif

#ifdef NUMA
#include <numa.h>
#endif

#include "defs.h"
#include "types.h"

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

#define HIGH numthreads
#define LOW numthreads_low

extern int numa;
extern int num_nodes[4];

struct thread_data {
  alignas(64) uint64_t begin;
  uint64_t end;
  bitboard occ;
  uint64_t *stats;
  int *p;
  int thread;
  int node;
  int thread_on_node;
};

struct Work {
  uint64_t *work;
  int numa;
  int total;
  int allocated;
};

typedef void (*WorkerFunc)(struct thread_data *);

void init_threads(int pawns);
void run_threaded(WorkerFunc func, struct Work *work, int max_threads,
    int report_time);
void run_single(WorkerFunc func, struct Work *work, int report_time);
void fill_work(uint64_t size, uint64_t mask, struct Work *w);
void fill_work_offset(uint64_t size, uint64_t mask, struct Work *w,
    uint64_t offset);
struct Work *alloc_work(int n);
struct Work *create_work(int n, uint64_t size, uint64_t mask);
struct Work *create_work_numa(int n, uint64_t *partition, uint64_t step,
    uint64_t mask);
struct Work *create_work_numa2(int n, uint64_t *stop);
void copy_work(struct Work **dst, struct Work *src);
void free_work(struct Work *work);

void create_compression_threads(void);
void run_compression(void (*func)(int t));

extern int numthreads, numthreads_low;
extern int thread_affinity;
extern struct thread_data *thread_data;
extern struct timeval start_time, cur_time;

#ifdef NUMA
void numa_init(void);
#endif

#endif
