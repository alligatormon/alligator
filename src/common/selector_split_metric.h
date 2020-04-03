#pragma once
#include "events/context_arg.h"
char* selector_split_metric(char *text, size_t sz, char *nsep, size_t nsep_sz, char *sep, size_t sep_sz, char *prefix, size_t prefix_size, char **maps, size_t maps_size, context_arg *carg);
