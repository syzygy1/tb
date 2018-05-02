/*
  Copyright (c) 2011-2013, 2018 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef __WIN32__
#include <pthread.h>
#else
#include <windows.h>
#endif
#include <sys/time.h>
#include "threads.h"
#include "util.h"

#ifdef NUMA
#include <numa.h>
#endif

int numa = 0;
int num_nodes[4] = { 1, 2, 4, 8 };
int th_limit = 0;

struct thread_data *thread_data;

#ifndef __WIN32__ /* pthread */

// implementation of pthread_barrier in case it is missing
#if !defined(_POSIX_BARRIERS) || !((_POSIX_BARRIERS - 20012L) >= 0)
#define pthread_barrier_t barrier_t
#define pthread_barrier_attr_t barrier_attr_t
#define pthread_barrier_init(b,a,n) barrier_init(b,n)
#define pthread_barrier_destroy(b) barrier_destroy(b)
#define pthread_barrier_wait(b) barrier_wait(b)

typedef struct {
  int needed;
  int called;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
} barrier_t;

int barrier_init(barrier_t *barrier, int needed)
{
  barrier->needed = needed;
  barrier->called = 0;
  pthread_mutex_init(&barrier->mutex, NULL);
  pthread_cond_init(&barrier->cond, NULL);
  return 0;
}

int barrier_destroy(barrier_t *barrier)
{
  pthread_mutex_destroy(&barrier->mutex);
  pthread_cond_destroy(&barrier->cond);
  return 0;
}

int barrier_wait(barrier_t *barrier)
{
  pthread_mutex_lock(&barrier->mutex);
  barrier->called++;
  if (barrier->called == barrier->needed) {
    barrier->called = 0;
    pthread_cond_broadcast(&barrier->cond);
  } else {
    pthread_cond_wait(&barrier->cond,&barrier->mutex);
  }
  pthread_mutex_unlock(&barrier->mutex);
  return 0;
}
#endif

pthread_t *threads;
static pthread_attr_t thread_attr;
static pthread_barrier_t barrier_start, barrier_end;
#define THREAD_FUNC void*

#else /* WIN32 */

HANDLE *threads;
HANDLE *start_event;
HANDLE *stop_event;
#define THREAD_FUNC DWORD

#endif

typedef void (*WorkerFunc)(struct thread_data *);

static WorkerFunc worker_func;
static int numa_threading;

struct Queue {
  alignas(64) uint64_t *work;
  int counter;
  int total;
};

static struct Queue queues[8];

int total_work;
int numthreads;
int thread_affinity;

struct timeval start_time, cur_time;

void fill_work(int n, uint64_t size, uint64_t mask, struct Work *work)
{
  int i;
  uint64_t *w = work->work[0];

  w[0] = 0;
  w[n] = size;

  for (i = 1; i < n; i++)
    w[i] = (((uint64_t)i * size) / (uint64_t)n) & ~mask;
}

void fill_work_offset(int n, uint64_t size, uint64_t mask, struct Work *w,
    uint64_t offset)
{
  fill_work(n, size, mask, w);
  for (int i = 0; i <= n; i++)
    w->work[0][i] += offset;
}

struct Work *alloc_work(int n)
{
  struct Work *w = malloc(sizeof(*w));
  w->numa = 0;
  w->work[0] = malloc((n + 1) * sizeof(uint64_t));
  return w;
}

struct Work *create_work(int n, uint64_t size, uint64_t mask)
{
  struct Work *w;

  w = alloc_work(n);
  fill_work(n, size, mask, w);

  return w;
}

struct Work *create_work_numa(int n, uint64_t *partition, uint64_t step,
    uint64_t mask)
{
  struct Work *w = malloc(sizeof(*w));
  w->numa = 1;
  uint64_t start = 0;
  for (int i = 0; i < num_nodes[numa]; i++) {
    w->work[i] = malloc((n + 1) * sizeof(uint64_t));
    w->work[i][0] = start;
    w->work[i][n] = start + partition[i] * step;
    for (int j = 1; j < n; j++)
      w->work[i][j] = start + ((j * partition[i] * step / n) & ~mask);
    start += partition[i] * step;
  }

  return w;
}

THREAD_FUNC worker(void *arg);

void init_threads(int pawns)
{
  int i;

  thread_data = alloc_aligned(numthreads * sizeof(*thread_data), 64);

  for (i = 0; i < numthreads; i++) {
    thread_data[i].thread = i;
    thread_data[i].node = i * num_nodes[numa] / numthreads;
  }

  if (pawns) {
    uint8_t *p = alloc_aligned(64 * numthreads, 64);
    for (i = 0; i < numthreads; i++)
      thread_data[i].p = (int *)(p + 64 * i);
  }

#ifndef __WIN32__

  threads = malloc(numthreads * sizeof(*threads));
  pthread_attr_init(&thread_attr);
  pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);
  pthread_barrier_init(&barrier_start, NULL, numthreads);
  pthread_barrier_init(&barrier_end, NULL, numthreads);

  for (i = 0; i < numthreads - 1; i++) {
    int rc = pthread_create(&threads[i], NULL, worker,
                                  (void *)&(thread_data[i]));
    if (rc) {
      fprintf(stderr, "ERROR: pthread_create() returned %d\n", rc);
      exit(EXIT_FAILURE);
    }
  }
  threads[numthreads - 1] = pthread_self();

#ifdef NUMA
  if (numa)
    numa_run_on_node(thread_data[numthreads - 1].node);
  else
#endif
  if (thread_affinity) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (i = 0; i < numthreads; i++) {
      CPU_SET(i, &cpuset);
      int rc = pthread_setaffinity_np(threads[i], sizeof(cpuset), &cpuset);
      CPU_CLR(i, &cpuset);
      if (rc)
        fprintf(stderr, "pthread_setaffinity_np() returned %d.\n", rc);
    }
  }

#else /* __WIN32__ */

  threads = malloc(numthreads * sizeof(*threads));
  start_event = malloc((numthreads - 1) * sizeof(*start_event));
  stop_event = malloc((numthreads - 1) * sizeof(*stop_event));
  for (i = 0; i < numthreads - 1; i++) {
    start_event[i] = CreateEvent(NULL, FALSE, FALSE, NULL);
    stop_event[i] = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!start_event[i] || !stop_event[i]) {
      fprintf(stderr, "CreateEvent() failed.\n");
      exit(EXIT_FAILURE);
    }
  }
  for (i = 0; i < numthreads - 1; i++) {
    threads[i] = CreateThread(NULL, 0, worker, (void *)&(thread_data[i]),
                                  0, NULL);
    if (threads[i] == NULL) {
      fprintf(stderr, "CreateThread() failed.\n");
      exit(EXIT_FAILURE);
    }
  }
  threads[numthreads - 1] = GetCurrentThread();

  if (thread_affinity)
    fprintf(stderr, "Thread affinities not yet implemented on Windows.\n");

#endif
}

THREAD_FUNC worker(void *arg)
{
  struct thread_data *thread = (struct thread_data *)arg;
  int t = thread->thread;
  int w;

#ifdef NUMA
  if (numa)
    numa_run_on_node(thread->node);
#endif

  do {
#ifndef __WIN32__
    pthread_barrier_wait(&barrier_start);
#else
    if (t != numthreads - 1)
      WaitForSingleObject(start_event[t], INFINITE);
    else {
      int i;
      for (i = 0; i < numthreads - 1; i++)
        SetEvent(start_event[i]);
    }
#endif

    struct Queue *queue = &queues[numa_threading ? thread->node : 0];
    int total = queue->total;
    WorkerFunc func = worker_func;
    if(numa || th_limit == 0 || t < th_limit - 1) {
      while (1) {
        w = __sync_fetch_and_add(&queue->counter, 1);
        if (w >= total) break;
        thread->begin = queue->work[w];
        thread->end = queue->work[w + 1];
        func(thread);
      }
    }

#ifndef __WIN32__
    pthread_barrier_wait(&barrier_end);
#else
  if (t != numthreads - 1)
    SetEvent(stop_event[t]);
  else
    WaitForMultipleObjects(numthreads - 1, stop_event, TRUE, INFINITE);
#endif
  } while (t != numthreads - 1);

  return 0;
}
void run_threaded(WorkerFunc func, struct Work *work, int report_time)
{
  run_threaded2(func, work, report_time, 0);
}
void run_threaded2(WorkerFunc func, struct Work *work, int report_time, int limit)
{
  int secs, usecs;
  struct timeval stop_time;
  th_limit = limit;
  worker_func = func;
  if (!work->numa) {
    numa_threading = 0;
    queues[0].work = work->work[0];
    queues[0].counter = 0;
    queues[0].total = total_work;
  } else {
    numa_threading = 1;
    for (int i = 0; i < num_nodes[numa]; i++) {
      queues[i].work = work->work[i];
      queues[i].counter = 0;
      queues[i].total = total_work;
    }
  }

  worker((void *)&(thread_data[numthreads - 1]));

  gettimeofday(&stop_time, NULL);
  secs = stop_time.tv_sec - cur_time.tv_sec;
  usecs = stop_time.tv_usec - cur_time.tv_usec;
  if (usecs < 0) {
    usecs += 1000000;
    secs--;
  }
  if (report_time)
    printf("time taken = %3d:%02d.%03d\n", secs / 60, secs % 60, usecs/1000);
  cur_time = stop_time;
}

void run_single(WorkerFunc func, struct Work *work, int report_time)
{
  int secs, usecs;
  struct timeval stop_time;
  struct thread_data *thread = &(thread_data[0]);

  thread->begin = work->work[0][0];
  thread->end = work->work[0][total_work];
  func(thread);

  gettimeofday(&stop_time, NULL);
  secs = stop_time.tv_sec - cur_time.tv_sec;
  usecs = stop_time.tv_usec - cur_time.tv_usec;
  if (usecs < 0) {
    usecs += 1000000;
    secs--;
  }
  if (report_time)
    printf("time taken = %3d:%02d.%03d\n", secs / 60, secs % 60, usecs/1000);
  cur_time = stop_time;
}

#ifdef NUMA
void numa_init(void)
{
  if (numa_available() == -1 || numa_max_node() == 0) {
    return;
  }

  int numa_nodes = numa_max_node() + 1;

  switch (numa_nodes) {
  case 2:
    numa = 1;
    break;
  case 4:
    numa = 2;
    break;
  case 8:
    numa = 3;
    break;
  default:
    fprintf(stderr, "Sorry, don't know how to handle %d NUMA nodes.\n",
        numa_nodes);
    exit(EXIT_FAILURE);
  }
}
#endif
