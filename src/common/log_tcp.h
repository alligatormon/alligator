#pragma once

#include "common/logs.h"

#define LOG_TCP_RECONNECT_MS 3000

typedef struct log_tcp_sink log_tcp_sink;

void log_tcp_sink_open(log_channel *ch, const char *host, int port);
void log_tcp_sink_close(log_channel *ch);
int log_tcp_sink_write(log_channel *ch, const char *data, size_t len);
int log_channel_is_tcp(const log_channel *ch);
