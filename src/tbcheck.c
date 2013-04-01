#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <getopt.h>

#include "defs.h"
#include "threads.h"
#include "checksum.h"

extern int total_work;
extern int numthreads;

static struct option options[] = {
  { "threads", 1, NULL, 't' },
  { "print", 0, NULL, 'p' },
  { 0, 0, NULL, 0 }
};

int main(int argc, char **argv)
{
  int i;
  int val, longindex;
  int only_print = 0;

  numthreads = 1;

  do {
    val = getopt_long(argc, argv, "t:p", options, &longindex);
    switch (val) {
    case 't':
      numthreads = atoi(optarg);
      break;
    case 'p':
      only_print = 1;
    }
  } while (val != EOF);

  if (optind >= argc) {
    printf("No tablebase specified.\n");
    exit(0);
  }

  if (numthreads < 1) numthreads = 1;
  else if (numthreads > MAX_THREADS) numthreads = MAX_THREADS;

  total_work = (numthreads == 1) ? 1 : 100 + 10 * numthreads;

  init_threads(0);

  if (!only_print)
    for (i = optind; i < argc; i++)
      verify_checksum(argv[i]);
  else
    for (i = optind; i < argc; i++)
      print_checksum(argv[i]);

  return 0;
}

