#pragma once
#include "metric/namespace.h"
#include "metric/metrictree.h"
#include "events/context_arg.h"
#define MAPPING_MAX_EXTRACT_FIELDS 64

void mapping_processing(context_arg *carg, metric_node *mnode, double dval);
void mapping_processing_foreach(context_arg *carg, metric_node *mnode, double dval);
void template_render(const char *template, char *fields[], int field_count, char *output);
int match_and_extract(const char *pattern, const char *str, char *fields[], int *num_fields);