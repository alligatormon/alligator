#pragma once
#include <stdint.h>
#include "common/selector.h"
#include "metric/percentile_heap.h"
#include "resolver/dns.h"
#include "common/url.h"

#define DNS_CLASS_CHAOS 3
#define DNS_CLASS_HESIOD 4
#define DNS_CLASS_ANY 255
#define DNS_MAX_RR_PER_DOMAIN 12
#define RESOLVER_QUERY_PERIOD_MS 10000
#define RESOLVER_METRIC_MIN_TTL_SEC 30

typedef struct dns_record {
	string *data;
	//char raw[16];
	uint32_t data_hash;
	uint64_t ttl;
} dns_record;

typedef struct dns_resource_records {
	dns_record *rr;
	uint64_t size;
	uint64_t index;
	uint16_t type;

	char *key;
	tommy_node node;
} dns_resource_records;

typedef struct resolver_data {
	host_aggregator_info *hi;
	
	percentile_buffer* response_time;
	percentile_buffer* read_time;
	percentile_buffer* write_time;
	alligator_ht *labels;
} resolver_data;

dns_resource_records* resolver_get_address(const char* domain, char* addrs, int naddr, const char* nameserver);
uint64_t dns_handler(char *metrics, size_t size, context_arg *carg);
string *resolver_get_api_response();
int is_ip_addr(char *str);
string* aggregator_get_addr(context_arg *carg, char *dname, uint16_t rrtype, uint32_t rclass);
/* In-flight hostname probes from aggregator_get_addr (not permanent dns aggregates). */
size_t resolver_ephemeral_count(void);
context_arg *resolver_ephemeral_lookup(const char *dname, uint16_t rrtype);
/* Close live handles, then carg_free. No-op if carg is in ac->aggregators. */
void resolver_ephemeral_release(context_arg *carg);
/* Failed attempt: abandon oneshot clients waiting on the probe's hostname,
 * then resolver_ephemeral_release(). Use on every no-answer outcome. */
void resolver_ephemeral_fail(context_arg *carg);
void resolver_ephemeral_note_closed(context_arg *carg);
/* uv_close cb for carg->resolver_timer: memset + note_closed if ephemeral freeing. */
void resolver_timer_handle_closed(uv_handle_t *handle);
/* Read-only cache lookup: return a cached, non-expired record for
 * "<dname>:<rrtype>" or NULL. Unlike aggregator_get_addr() this never starts
 * an async probe (used by embedders that manage their own resolution kickoff). */
string* resolver_cache_lookup(char *dname, uint16_t rrtype);
void resolver_start(uv_timer_t *timer);
char *resolver_carg_get_addr(context_arg *carg);
char* get_str_by_rrtype(uint16_t type);
uint16_t get_rrtype_by_str(char *rrtype);
context_arg* aggregator_push_addr_strtype(context_arg *carg, char *dname, char* strtype, uint32_t rclass);
uint64_t dns_init_type(char *domain, char *buf, uint16_t rrtype, uint16_t *transaction_id);
uint64_t dns_init_strtype(char *domain, char *buf, char *rrtype, uint16_t *transaction_id);
void resolver_query_refresh(context_arg *carg);
void resolver_connect_udp(void *arg);
void resolver_connect_tcp(void *arg);
/* Stop timers / pending; uv_close live client if any. Returns 1 if a close
 * callback will run (caller must wait), 0 otherwise. */
int resolver_udp_halt(context_arg *carg);
int resolver_tcp_halt(context_arg *carg);
void resolver_probe_halt(context_arg *carg);
/* Signal-safe: arm ephemeral/permanent probe closes. No nested uv_run. */
void resolver_probes_halt(void);
/* Post-loop only (after uv_stop): halt shared UDP binds, drain, finish wait_sends. */
void resolver_ephemeral_shutdown(void);
void resolver_udp_binds_halt(void);
int resolver_udp_pending_add(alligator_ht *pending, context_arg *carg);
void resolver_udp_pending_del(alligator_ht *pending, context_arg *carg);
context_arg *resolver_udp_match_pending(alligator_ht *pending, const char *buf, size_t nread);
void resolver_init_client_timer(context_arg *carg, void* timer_callback);
void dns_record_rule_push(char *dname, uint16_t rtype, void *data, uint64_t datalen, char *IP, uint64_t SIZE, uint64_t ttl);
int dns_name_decode_ext(const char* buf, char *msg, char* domain);
void resolver_del(context_arg *carg);
void resolver_del_json(json_t *resolver);
void dns_parser_push();
void resolver_push_json(json_t *resolver);
void resolver_stop();
