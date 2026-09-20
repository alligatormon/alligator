#pragma once
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include "events/context_arg.h"
#include "dstructures/ht.h"

/** labels_entrypoint: non-zero → type=entrypoint in series; zero → no-op (reserved). */
void entrypoint_read_metrics_throttled_push(context_arg *src, context_arg *carg, const char *proto, uint8_t labels_entrypoint, const char *host_label);

void aggregator_events_metric_add(context_arg *srv_carg, context_arg *carg, char *key, char *proto, char *type, char *host);

alligator_ht *alligator_event_labels(context_arg *carg, const char *proto, const char *type, const char *host);
void alligator_event_metric(const char *name, void *value, int8_t dtype, context_arg *carg, alligator_ht *base);
void alligator_session_connect_ok_set(context_arg *carg, uint64_t ok);
void alligator_session_connects_inc(context_arg *carg);
void alligator_parser_ok_set(context_arg *carg, uint64_t ok, const char *proto, const char *host);
void alligator_session_account_read(context_arg *carg, ssize_t nread);
void alligator_session_account_write(context_arg *carg, size_t nwrite);
