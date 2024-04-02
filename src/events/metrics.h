#pragma once
#include "events/context_arg.h"
void aggregator_events_metric_add(context_arg *srv_carg, context_arg *carg, char *key, char *proto, char *type, char *host);
