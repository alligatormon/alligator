#pragma once
#include "events/context_arg.h"
#include <stddef.h>
#include <stdint.h>

int entrypoint_compare(const void *arg, const void *obj);
int entrypoint_key_match_transport(const char *key, const char *prefix, const char *host, uint16_t port);
size_t entrypoint_collect_transport(const char *prefix, const char *host, uint16_t port, context_arg ***out);
void entrypoint_carg_replace_key(context_arg *carg, const char *fmt, ...);
