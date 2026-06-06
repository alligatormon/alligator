#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/logs.h"
#include "main.h"
#include "resolver/resolver.h"

#include <arpa/inet.h>

static char *dns_class_name(uint16_t rclass)
{
	if (rclass == DNS_CLASS_IN)
		return "IN";
	if (rclass == DNS_CLASS_CHAOS)
		return "CH";
	if (rclass == DNS_CLASS_HESIOD)
		return "HS";
	return "unknown";
}

// return ttl
uint64_t dns_handler(char *metrics, size_t size, context_arg *carg)
{
	dns_t resp;
	memset(&resp, 0, sizeof(resp));
	carg->parsed = 0;

	uint64_t rr_ttl = 3600;

	int nparse = dns_unpack(metrics, (int)size, &resp);
	if (nparse < 0 || (size_t)nparse != size) {
		carglog(carg, L_ERROR, "dns_unpack: ERR_INVALID_PACKAGE: %d != %zu\n", nparse, size);
		return rr_ttl;
	}

	dns_rr_t* rr = resp.answers;
	uint64_t addr_cnt = 0;
	if (resp.hdr.transaction_id != carg->packets_id ||
		resp.hdr.qr != DNS_RESPONSE ||
		resp.hdr.rcode != 0) {

		carglog(carg, L_ERROR, "resolve %s fail: ERR_MISMATCH\n", carg->host);
		dns_free(&resp);
		return rr_ttl;
	}

	if (resp.hdr.nanswer == 0)
	{
		dns_free(&resp);
		return rr_ttl;
	}

	if (!resp.hdr.nquestion || !resp.questions[0].name[0]) {
		carglog(carg, L_ERROR, "dns parse '%s': missing question name in response\n", carg->key);
		dns_free(&resp);
		return rr_ttl;
	}

	char *qname = resp.questions[0].name;
	char *qclass = dns_class_name(resp.questions[0].rclass);
	char *qtype = get_str_by_rrtype(resp.questions[0].rtype);
	if (!qtype)
		qtype = carg->rrtype ? carg->rrtype : "unknown";

	uint64_t val = 1;
	if (carg->resolver) {
		uint64_t period_sec = carg->period ? carg->period / 1000 : (RESOLVER_QUERY_PERIOD_MS / 1000);
		if (period_sec < 1)
			period_sec = 1;
		carg->curr_ttl = period_sec * 3;
	} else {
		carg->curr_ttl = rr_ttl;
	}
	carglog(carg, L_DEBUG, "dns resolved for key '%s', query '%s': %d entries\n", carg->key, qname, resp.hdr.nanswer);
	for (int i = 0; i < resp.hdr.nanswer; ++i, ++rr)
	{
		char *class = dns_class_name(rr->rclass);

		if (!carg->resolver && rr_ttl > rr->ttl) {
			rr_ttl = rr->ttl;
			carg->curr_ttl = rr_ttl;
		}

		carglog(carg, L_TRACE, "\tdns parse '%s', %d answer: class %s, ttl %"PRIu64", type: %d\n", carg->key, i, class, rr_ttl, rr->rtype);
		if (rr->rtype == DNS_TYPE_A) {
			if (rr->datalen == 4) {
				char ipAddress[20];
				uint8_t ipSize = snprintf(ipAddress, 19, "%hhu.%hhu.%hhu.%hhu", rr->data[0], rr->data[1], rr->data[2], rr->data[3]);
				dns_record_rule_push(qname, rr->rtype, rr->data, rr->datalen, ipAddress, ipSize, rr->ttl);

				carglog(carg, L_TRACE, "\tadd resolved address '%s' with A type to %s->%s\n", carg->key, qname, ipAddress);
				metric_add_labels4("aggregator_resolve_address", &val, DATATYPE_UINT, carg, "name", qname, "data", ipAddress, "type", "A", "class", class);
			}
			++addr_cnt;
		}
		else if (rr->rtype == DNS_TYPE_AAAA) {
			if (rr->datalen == 16) {
				uint8_t *data = (uint8_t*)rr->data;

				char ipAddress[41];
				uint8_t ipSize = snprintf(ipAddress, 40, "%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx", data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], data[8], data[9], data[10], data[11], data[12], data[13], data[14], data[15]);
				dns_record_rule_push(qname, rr->rtype, rr->data, rr->datalen, ipAddress, ipSize, rr->ttl);

				carglog(carg, L_TRACE, "\tadd resolved address '%s' with AAAA type to %s->%s\n", carg->key, qname, ipAddress);
				metric_add_labels4("aggregator_resolve_address", &val, DATATYPE_UINT, carg, "name", qname, "data", ipAddress, "type", "AAAA", "class", class);
			}
			++addr_cnt;
		}
		else if (rr->rtype == DNS_TYPE_CNAME) {
			char name[256];
			int rsize = dns_name_decode_ext(rr->data, metrics, name);
			carglog(carg, L_TRACE, "\tadd resolved address '%s' with CNAME type to %s->%s\n", carg->key, qname, name);
			metric_add_labels4("aggregator_resolve_address", &val, DATATYPE_UINT, carg, "name", qname, "data", name, "type", "CNAME", "class", class);
			dns_record_rule_push(qname, rr->rtype, rr->data, rr->datalen, name, rsize, rr->ttl);
		}
		else if (rr->rtype == DNS_TYPE_NS) {
			char name[256];
			int rsize = dns_name_decode_ext(rr->data, metrics, name);
			carglog(carg, L_TRACE, "\tadd resolved address '%s' with NS type to %s->%s\n", carg->key, qname, name);
			metric_add_labels4("aggregator_resolve_address", &val, DATATYPE_UINT, carg, "name", qname, "data", name, "type", "NS", "class", class);
			dns_record_rule_push(qname, rr->rtype, rr->data, rr->datalen, name, rsize, rr->ttl);
		}
		else if (rr->rtype == DNS_TYPE_PTR) {
			char name[256];
			int rsize = dns_name_decode_ext(rr->data, metrics, name);
			carglog(carg, L_TRACE, "\tadd resolved address '%s' with PTR type to %s->%s\n", carg->key, qname, name);
			metric_add_labels4("aggregator_resolve_address", &val, DATATYPE_UINT, carg, "name", qname, "data", name, "type", "PTR", "class", class);
			dns_record_rule_push(qname, rr->rtype, rr->data, rr->datalen, name, rsize, rr->ttl);
		}
		else if (rr->rtype == DNS_TYPE_MX) {
			char name[256];
			uint16_t priority_int = ntohs(*(uint16_t*)rr->data);
			char priority[8];
			snprintf(priority, sizeof(priority), "%hu", priority_int);
			int rsize = dns_name_decode_ext(rr->data + 2, metrics, name);
			carglog(carg, L_TRACE, "\tadd resolved address '%s' with MX type to %s->%s, priority %s\n", carg->key, qname, name, priority);
			metric_add_labels5("aggregator_resolve_address", &val, DATATYPE_UINT, carg, "name", qname, "data", name, "type", "MX", "class", class, "priority", priority);
			dns_record_rule_push(qname, rr->rtype, rr->data, rr->datalen, name, rsize, rr->ttl);
		}
		else if (rr->rtype == DNS_TYPE_SOA) {
			char nsname[256];
			char mxname[256];
			uint8_t *data = (uint8_t*)rr->data;
			int rsize = 0;
			int64_t serial = 0;
			int64_t refresh = 0;
			int64_t retry = 0;
			int64_t expire = 0;
			int64_t minimum = 0;

			rsize = dns_name_decode_ext((char*)data, metrics, nsname);

			data = data + rsize;
			rsize = dns_name_decode_ext((char*)data, metrics, mxname);

			data = data + rsize;
			serial = ((int64_t)data[0] << 24) | ((int64_t)data[1] << 16) | ((int64_t)data[2] << 8) | data[3];

			data = data + 4;
			refresh = ((int64_t)data[0] << 24) | ((int64_t)data[1] << 16) | ((int64_t)data[2] << 8) | data[3];

			data = data + 4;
			retry = ((int64_t)data[0] << 24) | ((int64_t)data[1] << 16) | ((int64_t)data[2] << 8) | data[3];

			data = data + 4;
			expire = ((int64_t)data[0] << 24) | ((int64_t)data[1] << 16) | ((int64_t)data[2] << 8) | data[3];

			data = data + 4;
			minimum = ((int64_t)data[0] << 24) | ((int64_t)data[1] << 16) | ((int64_t)data[2] << 8) | data[3];

			carglog(carg, L_TRACE, "\tadd resolved address '%s' with SOA '%s', nameserver '%s, mailserver '%s', serial %"PRId64", refresh %"PRId64", retry %"PRId64", expire %"PRId64", minimum %"PRId64"\n", carg->key, qname, nsname, mxname, serial, refresh, retry, expire, minimum);
			metric_add_labels5("aggregator_resolve_soa_serial", &serial, DATATYPE_INT, carg, "name", qname, "nameserver", nsname, "mailserver", mxname, "type", "SOA", "class", class);
			metric_add_labels5("aggregator_resolve_soa_refresh", &refresh, DATATYPE_INT, carg, "name", qname, "nameserver", nsname, "mailserver", mxname, "type", "SOA", "class", class);
			metric_add_labels5("aggregator_resolve_soa_retry", &retry, DATATYPE_INT, carg, "name", qname, "nameserver", nsname, "mailserver", mxname, "type", "SOA", "class", class);
			metric_add_labels5("aggregator_resolve_soa_expire", &expire, DATATYPE_INT, carg, "name", qname, "nameserver", nsname, "mailserver", mxname, "type", "SOA", "class", class);
			metric_add_labels5("aggregator_resolve_soa_minimum", &minimum, DATATYPE_INT, carg, "name", qname, "nameserver", nsname, "mailserver", mxname, "type", "SOA", "class", class);
		}
		else if (rr->rtype == DNS_TYPE_TXT) {
			char name[256];
			int rsize = dns_name_decode_ext(rr->data, metrics, name);
			carglog(carg, L_TRACE, "\tadd resolved address '%s' with TXT type to %s->%s\n", carg->key, qname, name);
			metric_add_labels4("aggregator_resolve_address", &val, DATATYPE_UINT, carg, "name", qname, "data", name, "type", "TXT", "class", class);
			dns_record_rule_push(qname, rr->rtype, rr->data, rr->datalen, name, rsize, rr->ttl);
		}
		else if (rr->rtype == DNS_TYPE_SRV) {
			char name[256];
			int rsize = dns_name_decode_ext(rr->data, metrics, name);
			carglog(carg, L_TRACE, "\tadd resolved address '%s' with SRV type to %s->%s\n", carg->key, qname, name);
			metric_add_labels4("aggregator_resolve_address", &val, DATATYPE_UINT, carg, "name", qname, "data", name, "type", "SRV", "class", class);
			dns_record_rule_push(qname, rr->rtype, rr->data, rr->datalen, name, rsize, rr->ttl);
		}
        else
			carglog(carg, L_ERROR, "\tadd resolved address '%s' error %s: unkown type %d\n", carg->key, qname, rr->rtype);
	}

	metric_add_labels3("aggregator_resolve_address_rr_count", &addr_cnt, DATATYPE_UINT, carg, "name", qname, "class", qclass, "type", qtype);

	if (carg->curr_ttl < RESOLVER_METRIC_MIN_TTL_SEC)
		carg->curr_ttl = RESOLVER_METRIC_MIN_TTL_SEC;

	if (carg->resolver)
		carglog(carg, L_DEBUG, "dns probe '%s': metric ttl %"PRIu64" sec\n", carg->key, carg->curr_ttl);

	dns_free(&resp);
	carg->parsed = 1;
	return rr_ttl;
}
