/*
  Copyright (c) 2011-2013 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "threads.h"

pthread_t threads[MAX_THREADS];
struct thread_data thread_data[MAX_THREADS];
static pthread_attr_t thread_attr;
static pthread_barrier_t barrier_start, barrier_end;

static struct {
  void (*func)(struct thread_data *);
  long64 *work;
  int counter;
  int total;
} queue;

int total_work;
int numthreads;

struct timeval start_time, cur_time;

void fill_work(int n, long64 size, long64 mask, long64 *w)
{
  int i;

  w[0] = 0;
  w[n] = size;

  for (i = 1; i < n; i++)
    w[i] = ((((long64)i) * size) / ((long64)n)) & ~mask;
}

long64 *alloc_work(int n)
{
  return (long64 *)malloc((n + 1) * sizeof(long64));
}

long64 *create_work(int n, long64 size, long64 mask)
{
  long64 *w;

  w = alloc_work(n);
  fill_work(n, size, mask, w);

  return w;
}

void *worker(void *arg);

void init_threads(int pawns)
{
  int i, rc;

  pthread_attr_init(&thread_attr);
  pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);

  for (i = 0; i < numthreads; i++)
    thread_data[i].thread = i;

  if (pawns) {
    void *p;
    posix_memalign(&p, 64, 64 * numthreads);
    for (i = 0; i < numthreads; i++)
      thread_data[i].p = (int *)(((ubyte *)p) + 64 * i);
  }

  pthread_barrier_init(&barrier_start, NULL, numthreads);
  pthread_barrier_init(&barrier_end, NULL, numthreads);

  for (i = 0; i < numthreads - 1; i++) {
    rc = pthread_create(&threads[i], NULL, worker, (void *)&(thread_data[i]));
    if (rc) {
      printf("ERROR: pthread_create() returned %d\n", rc);
      exit(-1);
    }
  }
}

void *worker(void *arg)
{
  struct thread_data *thread = (struct thread_data *)arg;
  int w;

  do {
    pthread_barrier_wait(&barrier_start);

    int total = queue.total;

    while (1) {
      w = __sync_fetch_and_add(&queue.counter, 1);
      if (w >= total) break;
      thread->begin = queue.work[w];
      thread->end = queue.work[w + 1];
      queue.func(thread);
    }

    pthread_barrier_wait(&barrier_end);
  } while (thread->thread != numthreads - 1);

  return 0;
}

void run_threaded(void (*func)(struct thread_data *), long64 *work, int report_time)
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

void run_single(void (*func)(struct thread_data *), long64 *work, int report_time)
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

