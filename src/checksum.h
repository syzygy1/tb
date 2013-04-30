/*
  Copyright (c) 2011-2013 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#ifndef CHECKSUM_H
#define CHECKSUM_H

void add_checksum(char *name);
void verify_checksum(char *name);
void print_checksum(char *name, char *sum);

#endif

