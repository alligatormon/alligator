#pragma once
#include <stddef.h>
#include "dstructures/tommy.h"

typedef enum {
	PG_BODY_TEXT = 0,
	PG_BODY_PROTO_DELIM,
	PG_BODY_PROTO_UNSUPPORTED,
} pg_body_format;

pg_body_format pushgateway_body_format(const char *headers, size_t headers_size);
alligator_ht *get_labels_from_url_pushgateway_format(char *uri, size_t uri_size, context_arg *carg);

/*
 * Pushgateway-compatible DELETE /metrics/job/<job>[/<label>/<value>...].
 *
 * Alligator keeps no grouping-key registry, so this is a selector delete:
 * every series in the carg namespace whose label set contains ALL URL grouping
 * labels is removed (any metric name). Upstream deletes only the exact grouping
 * key; here `/metrics/job/x` also removes series pushed under
 * `/metrics/job/x/instance/y`. A selector that matches nothing is a no-op.
 *
 * Returns the HTTP status to emit: 202 (accepted, also for no-op),
 * 400 (no `job` label, or an odd number of path components), 404 (no `/metrics/` path)
 * or 500 (out of memory; the series collected so far are still removed).
 */
int pushgateway_delete(const char *uri, size_t uri_size, context_arg *carg);
