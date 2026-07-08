#pragma once

#include "common/logs.h"

#define LOG_HTTP_RECONNECT_MS 3000

typedef struct log_http_sink log_http_sink;

void log_http_sink_open(log_channel *ch, const char *host, int port, const char *path);
void log_http_sink_close(log_channel *ch);
int log_http_sink_write(log_channel *ch, const char *data, size_t len);
int log_channel_is_http(const log_channel *ch);
