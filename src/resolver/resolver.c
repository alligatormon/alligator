#include "events/context_arg.h"
#include "common/aggregator.h"
#include "main.h"
#include "hdef.h"
#include "hsocket.h"
#include "herr.h"
#include "resolver/resolver.h"
#include "resolver/getaddrinfo.h"
#include "common/url.h"
#include "common/logs.h"
#include "metric/percentile_heap.h"

int resolver_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((dns_resource_records*)obj)->key;
	return strcmp(s1, s2);
}

void dns_record_rule_push(char *dname, uint16_t rtype, void *data, uint64_t datalen, char *IP, uint64_t SIZE, uint64_t ttl)
{
	char key[DNS_NAME_MAXLEN];
	snprintf(key, DNS_NAME_MAXLEN - 1, "%s:%hu", dname, rtype);
	uint32_t key_hash = tommy_strhash_u32(0, key);
	dns_resource_records *dns_rr = alligator_ht_search(ac->resolver, resolver_compare, key, key_hash);
	if (!dns_rr)
	{
		dns_rr = calloc(1, sizeof(*dns_rr));
		dns_rr->rr = calloc(1, sizeof(dns_record) * DNS_MAX_RR_PER_DOMAIN);
		dns_rr->type = rtype;
		dns_rr->key = strdup(key);
		alligator_ht_insert(ac->resolver, &(dns_rr->node), dns_rr, key_hash);
	}

	uint32_t DATA_hash = tommy_strhash_u32(0, IP);
	r_time time = setrtime();
	uint8_t nf = 1;
	for (uint64_t i = 0; i < dns_rr->size; ++i)
	{
		if (DATA_hash == dns_rr->rr[i].data_hash)
		{
			dns_rr->rr[i].ttl = time.sec + ttl;
			nf = 0;
		}
		else
		{
			if (dns_rr->rr[i].ttl <= ttl)
			{
				string_free(dns_rr->rr[i].data);
				for (uint64_t j = i + 1; j < dns_rr->size; ++j)
				{
					dns_rr->rr[j - 1].data = dns_rr->rr[j].data;
					dns_rr->rr[j - 1].ttl = dns_rr->rr[j].ttl;
					dns_rr->rr[j - 1].data_hash = dns_rr->rr[j].data_hash;
				}
				--dns_rr->size;
			}
		}
	}

	if (nf)
	{
		if (dns_rr->size == DNS_MAX_RR_PER_DOMAIN)
		{
			string_null(dns_rr->rr[dns_rr->index].data);
			string_cat(dns_rr->rr[dns_rr->index].data, IP, SIZE);
			dns_rr->rr[dns_rr->index].data_hash = DATA_hash;
			dns_rr->rr[dns_rr->index].ttl = time.sec + ttl;

			if (dns_rr->index + 1 == dns_rr->size)
				dns_rr->index = 0;
			else
				++dns_rr->index;
		}
		else
		{
			dns_rr->rr[dns_rr->size].data = string_init_dupn(IP, SIZE);
			dns_rr->rr[dns_rr->size].data_hash = DATA_hash;
			dns_rr->rr[dns_rr->size].ttl = time.sec + ttl;
			++dns_rr->size;
		}
	}
}

uint16_t get_rrtype_by_str(char *rrtype)
{
	if (!strncasecmp(rrtype, "any", 3))
		return DNS_TYPE_ANY;
	else if (!strncasecmp(rrtype, "aaaa", 4))
		return DNS_TYPE_AAAA;
	else if (*rrtype == 'a' || *rrtype == 'A')
		return DNS_TYPE_A;
	else if (!strncasecmp(rrtype, "txt", 3))
		return DNS_TYPE_TXT;
	else if (!strncasecmp(rrtype, "ns", 2))
		return DNS_TYPE_NS;
	else if (!strncasecmp(rrtype, "cname", 4))
		return DNS_TYPE_CNAME;
	else if (!strncasecmp(rrtype, "ptr", 3))
		return DNS_TYPE_PTR;
	else if (!strncasecmp(rrtype, "mx", 2))
		return DNS_TYPE_MX;
	else if (!strncasecmp(rrtype, "soa", 3))
		return DNS_TYPE_SOA;
	else if (!strncasecmp(rrtype, "srv", 3))
		return DNS_TYPE_SRV;

	return 0;
}

char* get_str_by_rrtype(uint16_t type)
{
	if (type == DNS_TYPE_AAAA)
		return "AAAA";
	else if (type == DNS_TYPE_A)
		return "A";
	else if (type == DNS_TYPE_CNAME)
		return "CNAME";
	else if (type == DNS_TYPE_MX)
		return "MX";
	else if (type == DNS_TYPE_NS)
		return "NS";
	else if (type == DNS_TYPE_TXT)
		return "TXT";
	else if (type == DNS_TYPE_SRV)
		return "SRV";
	else if (type == DNS_TYPE_PTR)
		return "PTR";

	return NULL;
}

uint64_t dns_init_type(char *domain, char *buf, uint16_t rrtype, uint16_t *transaction_id)
{
	dns_t *query = calloc(1, sizeof(*query));

	query->hdr.transaction_id = *transaction_id = getpid() + ac->ping_id++;
	query->hdr.qr = DNS_QUERY;
	query->hdr.rd = 1;
	query->hdr.nquestion = 1;

	dns_rr_t *question = calloc(1, sizeof(*question));
	strlcpy(question->name, domain, DNS_NAME_MAXLEN);

	question->rtype = rrtype;
	question->rclass = DNS_CLASS_IN;
	query->questions = question;

	uint64_t buflen = dns_pack(query, buf, 1024);
	free(query->questions);
	free(query);
	return buflen;
}

uint64_t dns_init_strtype(char *domain, char *buf, char *rrtype, uint16_t *transaction_id)
{
	uint16_t type = get_rrtype_by_str(rrtype);
	if (!type)
		type = DNS_TYPE_A;

	return dns_init_type(domain, buf, type, transaction_id);
}

int dns_name_decode_ext(const char* buf, char *msg, char* domain) {
    const char* p = buf;
    int len = *p++;
    //printf("len=%d\n", len);
    int buflen = 1;
    while (*p != '\0') {
        if (len-- == 0) {
            len = *p;
            //printf("len=%d\n", len);
            *domain = '.';
        }
        else {
            *domain = *p;
        }

	if (*p == '\300')
	{
		++p;
		p = msg + *p;
		//uint64_t copy_size = strlcpy(domain, msg + *p, 255);
		//buflen += copy_size;
		//domain += copy_size;
		//break;
	}

	if (!isalnum(*p) && *p != '-')
		*domain = '.';

        ++p;
        ++domain;
        ++buflen;
    }
    *domain = '\0';
    ++buflen; // include last '\0'
    return buflen;
}

//// return ttl
//uint64_t dns_handler(char *metrics, size_t size, context_arg *carg)
//{
//	dns_t resp;
//	memset(&resp, 0, sizeof(resp));
//	carg->parsed = 0;
//
//	uint64_t rr_ttl = 3600;
//
//	uint64_t nparse = dns_unpack(metrics, size, &resp);
//	if (nparse != size) {
//		if (carg->log_level > 1)
//			printf("dns_unpack: ERR_INVALID_PACKAGE: %"PRIu64" != %zu\n", nparse, size);
//		return rr_ttl;
//	}
//
//	dns_rr_t* rr = resp.answers;
//	int addr_cnt = 0;
//	if (resp.hdr.transaction_id != carg->packets_id ||
//		resp.hdr.qr != DNS_RESPONSE ||
//		resp.hdr.rcode != 0) {
//
//		if (carg->log_level > 1)
//			printf("resolve %s fail: ERR_MISMATCH\n", carg->host);
//		dns_free(&resp);
//		return rr_ttl;
//	}
//
//	if (resp.hdr.nanswer == 0)
//	{
//		dns_free(&resp);
//		return rr_ttl;
//	}
//
//	dns_resource_records *dns_rr = calloc(1, sizeof(*dns_rr));
//	uint64_t val = 1;
//	for (int i = 0; i < resp.hdr.nanswer; ++i, ++rr)
//	{
//		char *class = "unknown";
//		if (rr->rclass == DNS_CLASS_IN)
//			class = "IN";
//		else if (rr->rclass == DNS_CLASS_CHAOS)
//			class = "CH";
//		else if (rr->rclass == DNS_CLASS_HESIOD)
//			class = "HS";
//
//		if (rr_ttl > rr->ttl)
//			rr_ttl = rr->ttl;
//
//		if (rr->rtype == DNS_TYPE_A) {
//			if (rr->datalen == 4) {
//				//memcpy(addrs+addr_cnt, rr->data, rr->datalen);
//				char ipAddress[20];
//				uint8_t ipSize = snprintf(ipAddress, 19, "%hhu.%hhu.%hhu.%hhu", rr->data[0], rr->data[1], rr->data[2], rr->data[3]);
//				dns_record_rule_push(carg->data, rr->rtype, rr->data, rr->datalen, ipAddress, ipSize, rr->ttl);
//
//				metric_add_labels4("aggregator_resolve_address", &val, DATATYPE_UINT, carg, "name", carg->data, "data", ipAddress, "type", "A", "class", class);
//
//				//uint64_t ttl = rr->ttl;
//				//metric_add_labels3("aggregator_resolve_ttl", &ttl, DATATYPE_UINT, carg, "name", rr->name, "data", ipAddress, "type", "A", "class", class);
//			}
//			++addr_cnt;
//		}
//		if (rr->rtype == DNS_TYPE_AAAA) {
//			if (rr->datalen == 16) {
//				uint8_t *data = (uint8_t*)rr->data;
//
//				char ipAddress[41];
//				uint8_t ipSize = snprintf(ipAddress, 40, "%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx", data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], data[8], data[9], data[10], data[11], data[12], data[13], data[14], data[15]);
//				dns_record_rule_push(carg->data, rr->rtype, rr->data, rr->datalen, ipAddress, ipSize, rr->ttl);
//
//				metric_add_labels4("aggregator_resolve_address", &val, DATATYPE_UINT, carg, "name", carg->data, "data", ipAddress, "type", "AAAA", "class", class);
//			}
//			++addr_cnt;
//		}
//		else if (rr->rtype == DNS_TYPE_CNAME) {
//			char name[256];
//			int size = dns_name_decode_ext(rr->data, metrics, name);
//			metric_add_labels4("aggregator_resolve_address", &val, DATATYPE_UINT, carg, "name", carg->data, "data", name, "type", "CNAME", "class", class);
//			dns_record_rule_push(carg->data, rr->rtype, rr->data, rr->datalen, name, size, rr->ttl);
//		}
//		else if (rr->rtype == DNS_TYPE_NS) {
//			char name[256];
//			int size = dns_name_decode_ext(rr->data, metrics, name);
//			metric_add_labels4("aggregator_resolve_address", &val, DATATYPE_UINT, carg, "name", carg->data, "data", name, "type", "NS", "class", class);
//			dns_record_rule_push(carg->data, rr->rtype, rr->data, rr->datalen, name, size, rr->ttl);
//		}
//		else if (rr->rtype == DNS_TYPE_PTR) {
//			char name[256];
//			int size = dns_name_decode_ext(rr->data, metrics, name);
//			metric_add_labels4("aggregator_resolve_address", &val, DATATYPE_UINT, carg, "name", carg->data, "data", name, "type", "PTR", "class", class);
//			dns_record_rule_push(carg->data, rr->rtype, rr->data, rr->datalen, name, size, rr->ttl);
//		}
//		else if (rr->rtype == DNS_TYPE_MX) {
//			char name[256];
//			uint16_t priority_int = rr->data[1];
//			char priority[4];
//			snprintf(priority, 3, "%hhu", priority_int);
//			int size = dns_name_decode_ext(rr->data + 2, metrics, name);
//			metric_add_labels5("aggregator_resolve_address", &val, DATATYPE_UINT, carg, "name", carg->data, "data", name, "type", "MX", "class", class, "priority", priority);
//			dns_record_rule_push(carg->data, rr->rtype, rr->data, rr->datalen, name, size, rr->ttl);
//		}
//		else if (rr->rtype == DNS_TYPE_SOA) {
//			char nsname[256];
//			char mxname[256];
//			uint8_t *data = (uint8_t*)rr->data;
//			int size = 0;
//			int64_t serial = 0;
//			int64_t refresh = 0;
//			int64_t retry = 0;
//			int64_t expire = 0;
//			int64_t minimum = 0;
//
//			size = dns_name_decode_ext((char*)data, metrics, nsname);
//			//printf("NSname %s\n", nsname);
//
//			data = data + size;
//			size = dns_name_decode_ext((char*)data, metrics, mxname);
//			//printf("MX name %s\n", mxname);
//
//			data = data + size;
//			serial = (data[0] * pow(2, 24)) + (data[1] * pow(2, 16)) + (data[2] * pow(2, 8)) + data[3];
//			//printf("serial %"d64"\n", serial);
//
//			data = data + 4;
//			refresh = (data[0] * pow(2, 24)) + (data[1] * pow(2, 16)) + (data[2] * pow(2, 8)) + data[3];
//			//printf("refresh %"d64"\n", refresh);
//
//			data = data + 4;
//			retry = (data[0] * pow(2, 24)) + (data[1] * pow(2, 16)) + (data[2] * 256) + data[3];
//			//printf("retry %"d64"\n", retry);
//
//			data = data + 4;
//			expire = (data[0] * pow(2, 24)) + (data[1] * pow(2, 16)) + (data[2] * pow(2, 8)) + data[3];
//			//printf("expire %"d64"\n", expire);
//
//			data = data + 4;
//			minimum = (data[0] * pow(2, 24)) + (data[1] * pow(2, 16)) + (data[2] * pow(2, 8)) + data[3];
//			//printf("minimum %"d64"\n", minimum);
//
//			metric_add_labels5("aggregator_resolve_soa_serial", &serial, DATATYPE_INT, carg, "name", carg->data, "nameserver", nsname, "mailserver", mxname, "type", "SOA", "class", class);
//			metric_add_labels5("aggregator_resolve_soa_refresh", &refresh, DATATYPE_INT, carg, "name", carg->data, "nameserver", nsname, "mailserver", mxname, "type", "SOA", "class", class);
//			metric_add_labels5("aggregator_resolve_soa_retry", &retry, DATATYPE_INT, carg, "name", carg->data, "nameserver", nsname, "mailserver", mxname, "type", "SOA", "class", class);
//			metric_add_labels5("aggregator_resolve_soa_expire", &expire, DATATYPE_INT, carg, "name", carg->data, "nameserver", nsname, "mailserver", mxname, "type", "SOA", "class", class);
//			metric_add_labels5("aggregator_resolve_soa_minimum", &minimum, DATATYPE_INT, carg, "name", carg->data, "nameserver", nsname, "mailserver", mxname, "type", "SOA", "class", class);
//		}
//		else if (rr->rtype == DNS_TYPE_TXT) {
//			char name[256];
//			int size = dns_name_decode_ext(rr->data, metrics, name);
//			metric_add_labels4("aggregator_resolve_address", &val, DATATYPE_UINT, carg, "name", carg->data, "data", name, "type", "TXT", "class", class);
//			dns_record_rule_push(carg->data, rr->rtype, rr->data, rr->datalen, name, size, rr->ttl);
//		}
//		else if (rr->rtype == DNS_TYPE_SRV) {
//			char name[256];
//			int size = dns_name_decode_ext(rr->data, metrics, name);
//			metric_add_labels4("aggregator_resolve_address", &val, DATATYPE_UINT, carg, "name", carg->data, "data", name, "type", "SRV", "class", class);
//			dns_record_rule_push(carg->data, rr->rtype, rr->data, rr->datalen, name, size, rr->ttl);
//		}
//	}
//
//    	dns_free(&resp);
//	carg->parsed = 1;
//	return rr_ttl;
//}

void dns_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("dns");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = (void*)dns_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = NULL;
	strlcpy(actx->handler[0].key, "dns_stats", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

void resolver_start(uv_timer_t *timer)
{
	context_arg *carg = timer->data;
	if (carg->transport == APROTO_UDP)
		resolver_connect_udp(carg);
	else if (carg->transport == APROTO_TCP)
		resolver_connect_tcp(carg);
}

void resolver_carg_set_transport(context_arg *carg)
{
	srand(time(NULL));
	uint8_t r = rand() % ac->resolver_size;

	char *proto = ac->srv_resolver[r]->hi->proto == APROTO_TCP ? "tcp" : "udp";
	carg->transport_string = proto;
	carg->transport = carg->proto = ac->srv_resolver[r]->hi->proto;
	strlcpy(carg->host, ac->srv_resolver[r]->hi->host, HOSTHEADER_SIZE);
}

context_arg* aggregator_push_addr(context_arg *carg, char *dname, uint16_t rrtype, uint32_t rclass)
{
	if ((!ac->resolver_size) && (carg->transport == APROTO_RESOLVER)) {
		carglog(ac->system_carg, L_ERROR, "specified resolver '%s' in context %p, but there is no resolver directive, therefore alligator won't resolve this\n", carg->data, carg);
		return NULL;
	}

	if ((!ac->resolver_size) && (carg->parser_handler != dns_handler)) {
		resolver_getaddrinfo_get(carg);
		return NULL;
	}

	if (
        (!ac->resolver_size)
        && (rrtype == DNS_TYPE_A)
        && (rclass == DNS_CLASS_IN)
        && (carg->transport != APROTO_RESOLVER)
        && ((carg->parser_handler == dns_handler) && ((carg->transport == APROTO_UDP) || (carg->transport == APROTO_TCP)))
		&& (!carg->resolver)
        )
	{
		resolver_getaddrinfo_get(carg);
		return NULL;
	}

	char *buf = calloc(1, 1024);
	uint16_t transaction_id;
	uint64_t buflen = dns_init_type(dname, buf, rrtype, &transaction_id);


	context_arg* new_carg;
	if (carg->transport == APROTO_RESOLVER)
	{
		srand(time(NULL));
		uint8_t r = rand() % ac->resolver_size;
		new_carg = context_arg_json_fill(NULL, ac->srv_resolver[r]->hi, dns_handler, "dns_handler", NULL, 0, strdup(dname), NULL, 0, carg->loop, NULL, 1, NULL, 0);
		new_carg->labels = labels_dup(ac->srv_resolver[r]->labels);
		new_carg->rd = ac->srv_resolver[r];
	}
	else if (carg->parser_handler == dns_handler)
	{
		host_aggregator_info *hi = parse_url(carg->url, strlen(carg->url));
		new_carg = context_arg_json_fill(NULL, hi, dns_handler, "dns_handler", NULL, 0, strdup(dname), NULL, 0, carg->loop, NULL, 1, NULL, 0);
		new_carg->labels = labels_dup(carg->labels);
		url_free(hi);
	}
	else
	{
		srand(time(NULL));
		uint8_t r = rand() % ac->resolver_size;
		new_carg = context_arg_json_fill(NULL, ac->srv_resolver[r]->hi, dns_handler, "dns_handler", NULL, 0, strdup(dname), NULL, 0, carg->loop, NULL, 1, NULL, 0);
		new_carg->labels = labels_dup(ac->srv_resolver[r]->labels);
		new_carg->rd = ac->srv_resolver[r];
	}

	new_carg->follow_redirects = 1;
	new_carg->key = malloc(255);

	snprintf(new_carg->key, 254, "%s://%s/%s:%s:%u", carg->transport_string, new_carg->host, (char*)new_carg->data, "IN", rrtype);

	new_carg->packets_id = transaction_id;
	new_carg->log_level = carg->log_level;
	new_carg->resolver = 1;
	new_carg->rrtype = get_str_by_rrtype(rrtype);

	aconf_mesg_set(new_carg, buf, buflen);

	if (carg->log_level > 1)
		printf("aggregator_push_addr %p->%p(%p:%p) with key %s, hostname %s, dname '%s', rrtype: %u, port: %s tls: %d, timeout: %"u64"\n", carg, new_carg, &new_carg->connect, &new_carg->client, new_carg->key, new_carg->host, dname, rrtype, new_carg->port, new_carg->tls, new_carg->timeout);

	new_carg->resolver_timer.data = new_carg;
	uv_timer_init(new_carg->loop, &new_carg->resolver_timer);
	uv_timer_start(&new_carg->resolver_timer, resolver_start, 0, 10000);

	return new_carg;
}

string* aggregator_get_addr(context_arg *carg, char *dname, uint16_t rrtype, uint32_t rclass)
{
	char key[DNS_NAME_MAXLEN];
	snprintf(key, DNS_NAME_MAXLEN - 1, "%s:%hu", dname, rrtype);
	uint32_t key_hash = tommy_strhash_u32(0, key);

	if (carg->log_level > 1)
		printf("aggregator_get_addr '%s' %p(%p:%p) with key %s, hostname %s, dname '%s', port: %s tls: %d, timeout: %"u64"\n", key, carg, &carg->connect, &carg->client, carg->key, carg->host, dname, carg->port, carg->tls, carg->timeout);

	dns_resource_records *dns_rr = alligator_ht_search(ac->resolver, resolver_compare, key, key_hash);
	if (dns_rr)
	{
		srand(time(NULL));
		uint8_t r = rand() % dns_rr->size;
		return dns_rr->rr[r].data;
	}

	aggregator_push_addr(carg, dname, rrtype, rclass);
	return NULL;
}

string* aggregator_get_addr_strtype(context_arg *carg, char *dname, char* strtype, uint32_t rclass)
{
	uint16_t rrtype = get_rrtype_by_str(strtype);

	return aggregator_get_addr(carg, dname, rrtype, rclass);
}

context_arg* aggregator_push_addr_strtype(context_arg *carg, char *dname, char* strtype, uint32_t rclass)
{
	uint16_t rrtype = get_rrtype_by_str(strtype);

	if (carg->log_level > 1)
		printf("aggregator_push_addr_strtype %p(%p:%p) with key %s, hostname %s, dname '%s', port: %s tls: %d, timeout: %"u64"\n", carg, &carg->connect, &carg->client, carg->key, carg->host, dname, carg->port, carg->tls, carg->timeout);

	return aggregator_push_addr(carg, dname, rrtype, rclass);
}

void resolver_push_json(json_t *resolver)
{
	char *host = (char*)json_string_value(resolver);
	size_t size = json_string_length(resolver);
	//ac->hi_resolver[ac->resolver_size] = parse_url(host, size);

	ac->srv_resolver[ac->resolver_size] = calloc(1, sizeof(resolver_data));
	ac->srv_resolver[ac->resolver_size]->response_time = init_percentile_buffer(percentile_init_3n(99, 95, 90), 3);
	ac->srv_resolver[ac->resolver_size]->read_time = init_percentile_buffer(percentile_init_3n(99, 95, 90), 3);
	ac->srv_resolver[ac->resolver_size]->write_time = init_percentile_buffer(percentile_init_3n(99, 95, 90), 3);
	ac->srv_resolver[ac->resolver_size]->hi = parse_url(host, size);
	ac->srv_resolver[ac->resolver_size]->labels = alligator_ht_init(NULL);
	labels_hash_insert_nocache(ac->srv_resolver[ac->resolver_size]->labels, "host", ac->srv_resolver[ac->resolver_size]->hi->url);

	++ac->resolver_size;
}

void resolver_del_json(json_t *resolver)
{
	//char *host = json_string_value(resolver);
	//size_t size = json_string_length(resolver);
	//url_free(ac->hi_resolver[ac->resolver_size])
}

void resolver_del(context_arg *carg)
{
	free(carg->data);
	carg_free(carg);
}

void resolver_stop_foreach(void *funcarg, void* arg)
{
	dns_resource_records *dns_rr = arg;
	for (uint64_t i = 0; i < dns_rr->size; ++i)
		string_free(dns_rr->rr[i].data);

	free(dns_rr->key);
	free(dns_rr->rr);
	free(dns_rr);
}

void resolver_stop()
{
	alligator_ht_foreach_arg(ac->resolver, resolver_stop_foreach, NULL);
	alligator_ht_done(ac->resolver);
	free(ac->resolver);

	if (ac->resolver_size)
	{
		while (ac->resolver_size != 0)
			url_free(ac->srv_resolver[ac->resolver_size--]->hi);
		url_free(ac->srv_resolver[0]->hi);
	}
}
