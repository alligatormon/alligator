#pragma once
#include <jansson.h>
#include "metric/metrictree.h"
#define MAPPING_MATCH_GLOB 0
#define MAPPING_MATCH_PCRE 1
#define MAPPING_MATCH_GROK 2
mapping_metric* json_mapping_parser(json_t *mapping);
void push_mapping_metric(mapping_metric *dest, mapping_metric *source);
mapping_metric* mapping_copy(mapping_metric *src);
void mm_list(mapping_metric *dest);
void mapping_free_recurse(mapping_metric *src);
