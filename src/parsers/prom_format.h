#pragma once
//#include "rbtree.h"

void metric_labels_parse(char *metric, size_t l, char *get);
void prom_getmetrics_n (char *buf, size_t len, char *get);
//alligator_labels* labels_parse(char *lblstr, size_t l);
