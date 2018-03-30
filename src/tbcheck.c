#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>

#include "defs.h"
#include "threads.h"
#include "checksum.h"

extern int total_work;
extern int numthreads;

static void compare_checksums(char *file)
{
  char name[128];
  char sum[40];
  char sum2[40];

  FILE *F = fopen(file, "r");
  if (!F) {
    fprintf(stderr, "Could not open %s.\n", file);
    return;
  }
  while (!feof(F)) {
    int num = fscanf(F, "%100[KQRBNP|v|rtbwz|.]: %32[0-9|a-f]\n", name, sum);
    if (num != 2 || strlen(sum) != 32) {
      fprintf(stderr, "Could not completely parse %s.\n", file);
      break;
    }
    FILE *G = fopen(name, "rb");
    if (!G) {
      fprintf(stderr, "Tablebase file %s not found.\n", name);
    } else {
      fclose(G);
      print_checksum(name, sum2);
      if (strcmp(sum, sum2) == 0)
	printf("%s: OK!\n", name);
      else
	printf("%s: FAIL!\n", name);
    }
  }
  fclose(F);
}

static struct option options[] = {
  { "threads", 1, NULL, 't' },
  { "print", 0, NULL, 'p' },
  { "compare", 0, NULL, 'c' },
  { 0, 0, NULL, 0 }
};

int main(int argc, char **argv)
{
  int i;
  int val, longindex;
  int only_print = 0;
  int compare = 0;

  numthreads = 1;

  do {
    val = getopt_long(argc, argv, "t:pc", options, &longindex);
    switch (val) {
    case 't':
      numthreads = atoi(optarg);
      break;
    case 'p':
      only_print = 1;
      break;
    case 'c':
      compare = 1;
      break;
    }
  } while (val != EOF);

  if (optind >= argc) {
    fprintf(stderr, "No tablebase specified.\n");
    exit(0);
  }

  if (numthreads < 1) numthreads = 1;

  total_work = (numthreads == 1) ? 1 : 100 + 10 * numthreads;

  init_threads(0);

  if (!compare) {
    if (!only_print)
      for (i = optind; i < argc; i++)
	verify_checksum(argv[i]);
    else
      for (i = optind; i < argc; i++) {
	char sum[40];
	printf("%s: ", argv[i]);
	print_checksum(argv[i], sum);
	puts(sum);
      }
  } else {
    for (i = optind; i < argc; i++)
      compare_checksums(argv[i]);
  }

  return 0;
}

