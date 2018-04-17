/*
  Copyright (c) 2011-2013, 2018 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

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
  pthread_mutex_init(&barrier->mutex,NULL);
  pthread_cond_init(&barrier->cond,NULL);
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

static struct {
  void (*func)(struct thread_data *);
  uint64_t *work;
  int counter;
  int total;
} queue;

int total_work;
int numthreads;
int thread_affinity;

struct timeval start_time, cur_time;

void fill_work(int n, uint64_t size, uint64_t mask, uint64_t *w)
{
  int i;

  w[0] = 0;
  w[n] = size;

  for (i = 1; i < n; i++)
    w[i] = ((((uint64_t)i) * size) / ((uint64_t)n)) & ~mask;
}

void fill_work_offset(int n, uint64_t size, uint64_t mask, uint64_t *w,
    uint64_t offset)
{
  fill_work(n, size, mask, w);
  for (int i = 0; i <= n; i++)
    w[i] += offset;
}

uint64_t *alloc_work(int n)
{
  return (uint64_t *)malloc((n + 1) * sizeof(uint64_t));
}

uint64_t *create_work(int n, uint64_t size, uint64_t mask)
{
  uint64_t *w;

  w = alloc_work(n);
  fill_work(n, size, mask, w);

  return w;
}

THREAD_FUNC worker(void *arg);

void init_threads(int pawns)
{
  int i;

  thread_data = malloc(numthreads * sizeof(*thread_data));

  for (i = 0; i < numthreads; i++)
    thread_data[i].thread = i;

  if (pawns) {
    uint8_t *p = alloc_aligned(64 * numthreads, 64);
    for (i = 0; i < numthreads; i++)
      thread_data[i].p = (int *)(p + 64 * i);
  }

#ifndef __WIN32__
  threads = malloc((numthreads - 1) * sizeof(*threads));
  pthread_attr_init(&thread_attr);
  pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);
  pthread_barrier_init(&barrier_start, NULL, numthreads);
  pthread_barrier_init(&barrier_end, NULL, numthreads);

  for (i = 0; i < numthreads - 1; i++) {
    int rc = pthread_create(&threads[i], NULL, worker,
                                  (void *)&(thread_data[i]));
    if (rc) {
      fprintf(stderr, "ERROR: pthread_create() returned %d\n", rc);
      exit(1);
    }
  }

  if (thread_affinity) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
    if (rc)
      fprintf(stderr, "pthread_setaffinity_np() returned %d.\n", rc);
    CPU_CLR(0, &cpuset);
    for (i = 1; i < numthreads; i++) {
      CPU_SET(i, &cpuset);
      rc = pthread_setaffinity_np(threads[i - 1], sizeof(cpuset), &cpuset);
      CPU_CLR(i, &cpuset);
      if (rc)
        fprintf(stderr, "pthread_setaffinity_np() returned %d.\n", rc);
    }
  }
#else
  threads = malloc((numthreads - 1) * sizeof(*threads));
  start_event = malloc((numthreads - 1) * sizeof(*start_event));
  stop_event = malloc((numthreads - 1) * sizeof(*stop_event));
  for (i = 0; i < numthreads - 1; i++) {
    start_event[i] = CreateEvent(NULL, FALSE, FALSE, NULL);
    stop_event[i] = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!start_event[i] || !stop_event[i]) {
      fprintf(stderr, "CreateEvent() failed.\n");
      exit(1);
    }
  }
  for (i = 0; i < numthreads - 1; i++) {
    threads[i] = CreateThread(NULL, 0, worker, (void *)&(thread_data[i]),
                                  0, NULL);
    if (threads[i] == NULL) {
      fprintf(stderr, "CreateThread() failed.\n");
      exit(1);
    }
  }

  if (thread_affinity)
    fprintf(stderr, "Thread affinities not yet implemented on Windows.\n");
#endif
}

THREAD_FUNC worker(void *arg)
{
  struct thread_data *thread = (struct thread_data *)arg;
  int t = thread->thread;
  int w;

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

    int total = queue.total;

    while (1) {
      w = __sync_fetch_and_add(&queue.counter, 1);
      if (w >= total) break;
      thread->begin = queue.work[w];
      thread->end = queue.work[w + 1];
      queue.func(thread);
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

void run_threaded(void (*func)(struct thread_data *), uint64_t *work, int report_time)
{
  int secs, usecs;
  struct timeval stop_time;

  queue.func = func;
  queue.work = work;
  queue.total = total_work;
  queue.counter = 0;

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

void run_single(void (*func)(struct thread_data *), uint64_t *work, int report_time)
{
  int secs, usecs;
  struct timeval stop_time;
  struct thread_data *thread = &(thread_data[0]);

  thread->begin = work[0];
  thread->end = work[total_work];
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
