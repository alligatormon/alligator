#pragma once

#include "dstructures/ht.h"
#include "jansson.h"
#include "events/context_arg.h"
#include "common/multiline.h"
#include "vrl.h"
#include <uv.h>

/*
 * Alligator glue for avrl (Vector Remap Language).
 *
 * Config surface uses the name "vrl" everywhere (no amtail/mtail split):
 *   vrl { name foo; script /path/to/prog.vrl; }
 *   vrl { name bar; program ".status = upcase(.message)"; }
 *   aggregate { vrl file:///var/log/app.log name=foo; }
 *   entrypoint { handler vrl; vrl foo; }
 *
 * Multiline (Vector-compatible, shared with mtail/grok):
 *   On aggregate:
 *     start_pattern=^\S condition_pattern=^\s multiline_mode=continue_through
 *   Or on the vrl program / JSON:
 *     "multiline": {
 *       "start_pattern": "^\\S",
 *       "condition_pattern": "^\\s",
 *       "mode": "continue_through"
 *     }
 *   Legacy: "multiline": { "mode": "halt_before", "pattern": "^\\S" }
 *   (pattern is used for both start and condition).
 *
 * Metric convention after a successful transform: emit `.metrics` array of
 *   { "name": "...", "value": <number>, "labels": { "k": "v" } }
 * or a single `.metric` object with the same shape.
 */

typedef struct vrl_node {
	char *name;
	char *key;
	char *script;   /* file path, optional */
	char *program;  /* inline source, optional (script XOR program) */
	vrl_program *prog;
	avrl_log_level ll;

	/* optional multiline assembler config (owned strings) */
	char *ml_start_pattern;
	char *ml_condition_pattern;
	alligator_ml_mode ml_mode;
	uint8_t ml_enabled;

	uv_mutex_t lock;
	tommy_node node;
} vrl_node;

int vrl_node_compare(const void *arg, const void *obj);
vrl_node *vrl_node_get(char *name);
vrl_node *vrl_node_get_any(void);
void vrl_node_free(vrl_node *vn);

int vrl_engine_init(void);
void vrl_engine_free(void);
void vrl_parser_push(void);

int vrl_del(char *name);
int vrl_push(json_t *cfg);

void vrl_handler(char *metrics, size_t size, context_arg *carg);
void vrl_stream_free(context_arg *carg);
