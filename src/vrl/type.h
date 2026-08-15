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

	/* Async DNS glue (dns_lookup / reverse_dns): how long a record may pause
	 * waiting for a first-sight resolution, and how often to re-check cache.
	 * Config accepts human units (dns_timeout 2s) or *_ms integer aliases. */
	uint64_t dns_timeout_ms;
	uint64_t dns_poll_ms;

	/* Negative cache: remember names that failed/timed out for this long (ms)
	 * so later occurrences return null immediately instead of re-suspending.
	 * 0 disables it (default). Smaller values re-resolve bad names sooner.
	 * dns_negative_cache_max caps distinct entries to bound memory. */
	uint64_t dns_negative_ttl_ms;
	uint64_t dns_negative_cache_max;

	uv_mutex_t lock;
	tommy_node node;
} vrl_node;

#define VRL_DNS_KEY_MAX 256
#define VRL_DNS_DEFAULT_TIMEOUT_MS 2000
#define VRL_DNS_DEFAULT_POLL_MS 50
#define VRL_DNS_DEFAULT_NEGATIVE_CACHE_MAX 100000

/* One buffered, not-yet-executed record captured while a stream is paused
 * waiting for async DNS. Bytes are owned. */
typedef struct vrl_record_item {
	char *data;
	size_t len;
} vrl_record_item;

/*
 * Per-stream avrl runtime state (opaque `void *vrl_stream` on context_arg).
 *
 * DNS suspend/resume: when dns_lookup()/reverse_dns() miss the cache on first
 * sight, the builtin marks the stream suspended and starts async resolution.
 * vrl_run_record() then discards the un-exported record, buffers it as
 * pending_record, and further records for this source are queued (not run) so
 * output order is preserved. A poll timer re-runs the pending record once the
 * answer is cached (or, after dns_timeout_ms, logs L_ERROR and re-runs with a
 * null result) and drains the queue.
 */
typedef struct vrl_stream {
	vrl_ctx *ctx;
	vrl_node *vn; /* borrowed, under vn->lock while running */
	context_arg *carg;
	int ok;

	/* --- async DNS / HTTP suspend state --- */
	int dns_suspended;                 /* stream paused waiting for DNS or HTTP */
	int dns_force_null;                /* next lookup of (dns_name,rrtype) returns null (timeout) */
	int await_http;                    /* 1 = paused on http_request, 0 = paused on DNS */
	int http_force_null;               /* next http_request of http_url returns null (timeout) */
	char dns_name[VRL_DNS_KEY_MAX];    /* query name awaited (host, or *.arpa for PTR) */
	char http_url[2048];               /* URL awaited by http_request */
	uint16_t dns_rrtype;               /* rrtype awaited (A / PTR) */
	uint64_t dns_deadline_ms;          /* uv_now() deadline for the current wait */
	uint64_t dns_timeout_ms;           /* copied from vn (or default); also used for HTTP */
	uint64_t dns_poll_ms;              /* copied from vn (or default) */
	uint64_t dns_negative_ttl_ms;      /* copied from vn; 0 = negative cache off */

	char *pending_record;              /* owned: record that suspended, to re-run */
	size_t pending_len;

	/* FIFO of records captured while suspended (ordered, owned bytes) */
	vrl_record_item *queue;
	size_t q_head;
	size_t q_len;
	size_t q_cap;

	uv_timer_t *resume_timer;          /* owned; poll for resolution */
} vrl_stream;

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

/* Register alligator host builtins (dns_lookup, reverse_dns, http_request)
 * into avrl. Called once from vrl_engine_init() after vrl_stdlib_init(). */
void vrl_host_builtins_init(void);
void vrl_http_builtins_init(void);
int  vrl_http_cache_is_ready(const char *url);
void vrl_http_cache_force_ready_null(const char *url);
void vrl_http_metric(const char *result, uint64_t start_ms, uint64_t now_ms);
/* Build a reverse-DNS query name for an IPv4/IPv6 literal:
 *   "1.2.3.4"  -> "4.3.2.1.in-addr.arpa"
 *   "2001:db8::1" -> nibble-reversed "...ip6.arpa"
 * Returns 1 on success (out is NUL-terminated), 0 if ip is not a valid literal. */
int vrl_dns_reverse_name(const char *ip, char *out, size_t outlen);
/* Emit DNS resolution metrics against ac->system_carg:
 *   vrl_dns_resolutions_total{type,result} and
 *   vrl_dns_resolution_duration_seconds_sum{type,result}
 * result is one of "success", "failure", "timeout". */
void vrl_dns_metric(const char *type, const char *result,
		    uint64_t start_ms, uint64_t now_ms);

/* Negative DNS cache (bad-name memoization), keyed by "<name>:<rrtype>".
 * vrl_dns_neg_active() reports whether a live negative entry exists (and lazily
 * evicts expired ones); vrl_dns_neg_add() records/refreshes one with ttl_ms,
 * bounded by cap (0 = use default). All are safe to call from the loop thread
 * and from vrl record processing. */
int  vrl_dns_neg_active(const char *key, uint64_t now_ms);
void vrl_dns_neg_add(const char *key, uint64_t now_ms, uint64_t ttl_ms, uint64_t cap);
