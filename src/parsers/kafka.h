#pragma once

#include "events/context_arg.h"

void kafka_parser_push(void);

/* Query-string helpers used by the parser and unit tests. */
int kafka_query_param(const char *query, const char *name, char *out, size_t outlen);
int kafka_name_allowed(const char *name, const char *include_re, const char *exclude_re);
