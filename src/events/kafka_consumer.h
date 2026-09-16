#pragma once

#include "events/context_arg.h"

/* Start a persistent Kafka consumer for carg.
 * URL form: kafka://brokers/topic?librdkafka_opts
 * Topic and opts come from carg->query_url (path without leading slash).
 * Returns a non-NULL type string on success. */
char *kafka_consumer_handler(context_arg *carg);

/* Tear down an active kafka consumer. */
void kafka_consumer_handler_del(context_arg *carg);

/* Split query_url "topic" or "topic?opts" into topic and opts.
 * On success returns 0 and sets *topic (malloc'd). *opts is malloc'd or NULL.
 * Used by the consumer and unit tests. */
int kafka_consumer_split_query(const char *query_url, char **topic, char **opts);
