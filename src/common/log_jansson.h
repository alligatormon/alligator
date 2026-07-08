#pragma once

#include <jansson.h>
#include <stddef.h>

char *log_jansson_dumps_compact(const json_t *doc, size_t *outlen);
char *log_jansson_dumps_line(const json_t *doc, size_t *outlen);
