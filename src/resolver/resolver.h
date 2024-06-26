#pragma once
#include <stdint.h>
#include "common/selector.h"
#include "metric/percentile_heap.h"
#include "dns.h"
#include "common/url.h"

#define DNS_TYPE_TXT    15
#define DNS_TYPE_SRV    33
#define DNS_CLASS_CHAOS 3
#define DNS_CLASS_HESIOD 4
#define DNS_CLASS_ANY 255
#define DNS_MAX_RR_PER_DOMAIN 12

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
string* aggregator_get_addr(context_arg *carg, char *dname, uint16_t rrtype, uint32_t rclass);
void resolver_start(uv_timer_t *timer);
char *resolver_carg_get_addr(context_arg *carg);
char* get_str_by_rrtype(uint16_t type);
uint16_t get_rrtype_by_str(char *rrtype);
context_arg* aggregator_push_addr_strtype(context_arg *carg, char *dname, char* strtype, uint32_t rclass);
uint64_t dns_init_strtype(char *domain, char *buf, char *rrtype, uint16_t *transaction_id);
void resolver_connect_udp(void *arg);
void resolver_connect_tcp(void *arg);
void resolver_init_client_timer(context_arg *carg, void* timer_callback);
void dns_record_rule_push(char *dname, uint16_t rtype, void *data, uint64_t datalen, char *IP, uint64_t SIZE, uint64_t ttl);
int dns_name_decode_ext(const char* buf, char *msg, char* domain);
void resolver_del(context_arg *carg);
void resolver_del_json(json_t *resolver);
void dns_parser_push();
void resolver_push_json(json_t *resolver);
void resolver_stop();
