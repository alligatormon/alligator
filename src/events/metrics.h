#pragma once
#include "events/context_arg.h"

/** labels_entrypoint: non-zero → type=entrypoint in series; zero → no-op (reserved). */
void entrypoint_read_metrics_throttled_push(context_arg *src, context_arg *carg, const char *proto, uint8_t labels_entrypoint, const char *host_label);

void aggregator_events_metric_add(context_arg *srv_carg, context_arg *carg, char *key, char *proto, char *type, char *host);
