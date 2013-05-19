#ifndef TBPROBE_H
#define TBPROBE_H

void init_tablebases(void);
int probe_wdl(Position& pos, int *success);
int probe_dtz(Position& pos, int *success);
int root_probe(Position& pos);

#endif

