#pragma once

#include "common/logs.h"

typedef struct log_kafka_sink log_kafka_sink;

void log_kafka_sink_open(log_channel *ch, const char *dest);
void log_kafka_sink_close(log_channel *ch);
int log_kafka_sink_write(log_channel *ch, const char *data, size_t len);
int log_channel_is_kafka(const log_channel *ch);
uint64_t log_kafka_sink_dropped(const log_channel *ch);
