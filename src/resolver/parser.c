#include "events/context_arg.h"
#include "common/aggregator.h"
#include "main.h"
#include "hdef.h"
#include "hsocket.h"
#include "herr.h"
#include "resolver/resolver.h"

// return ttl
uint64_t dns_handler(char *metrics, size_t size, context_arg *carg)
{
	dns_t resp;
	memset(&resp, 0, sizeof(resp));
	carg->parsed = 0;

	uint64_t rr_ttl = 3600;

	uint64_t nparse = dns_unpack(metrics, size, &resp);
	if (nparse != size) {
		if (carg->log_level > 1)
			printf("dns_unpack: ERR_INVALID_PACKAGE: %"PRIu64" != %zu\n", nparse, size);
		return rr_ttl;
	}

	dns_rr_t* rr = resp.answers;
	uint64_t addr_cnt = 0;
	if (resp.hdr.transaction_id != carg->packets_id ||
		resp.hdr.qr != DNS_RESPONSE ||
		resp.hdr.rcode != 0) {

		if (carg->log_level > 1)
			printf("resolve %s fail: ERR_MISMATCH\n", carg->host);
		dns_free(&resp);
		return rr_ttl;
	}

	if (resp.hdr.nanswer == 0)
	{
		dns_free(&resp);
		return rr_ttl;
	}

	dns_resource_records *dns_rr = calloc(1, sizeof(*dns_rr));
	uint64_t val = 1;
	for (int i = 0; i < resp.hdr.nanswer; ++i, ++rr)
	{
		char *class = "unknown";
		if (rr->rclass == DNS_CLASS_IN)
			class = "IN";
		else if (rr->rclass == DNS_CLASS_CHAOS)
			class = "CH";
		else if (rr->rclass == DNS_CLASS_HESIOD)
			class = "HS";

		if (rr_ttl > rr->ttl)
			rr_ttl = rr->ttl;

		if (rr->rtype == DNS_TYPE_A) {
			if (rr->datalen == 4) {
				//memcpy(addrs+addr_cnt, rr->data, rr->datalen);
				char ipAddress[20];
				uint8_t ipSize = snprintf(ipAddress, 19, "%hhu.%hhu.%hhu.%hhu", rr->data[0], rr->data[1], rr->data[2], rr->data[3]);
				dns_record_rule_push(carg->data, rr->rtype, rr->data, rr->datalen, ipAddress, ipSize, rr->ttl);

				metric_add_labels4("aggregator_resolve_address", &val, DATATYPE_UINT, carg, "name", carg->data, "data", ipAddress, "type", "A", "class", class);

				//uint64_t ttl = rr->ttl;
				//metric_add_labels3("aggregator_resolve_ttl", &ttl, DATATYPE_UINT, carg, "name", rr->name, "data", ipAddress, "type", "A", "class", class);
			}
			++addr_cnt;
		}
		if (rr->rtype == DNS_TYPE_AAAA) {
			if (rr->datalen == 16) {
				uint8_t *data = (uint8_t*)rr->data;

				char ipAddress[41];
				uint8_t ipSize = snprintf(ipAddress, 40, "%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx", data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], data[8], data[9], data[10], data[11], data[12], data[13], data[14], data[15]);
				dns_record_rule_push(carg->data, rr->rtype, rr->data, rr->datalen, ipAddress, ipSize, rr->ttl);

				metric_add_labels4("aggregator_resolve_address", &val, DATATYPE_UINT, carg, "name", carg->data, "data", ipAddress, "type", "AAAA", "class", class);
			}
			++addr_cnt;
		}
		else if (rr->rtype == DNS_TYPE_CNAME) {
			char name[256];
			int size = dns_name_decode_ext(rr->data, metrics, name);
			metric_add_labels4("aggregator_resolve_address", &val, DATATYPE_UINT, carg, "name", carg->data, "data", name, "type", "CNAME", "class", class);
			dns_record_rule_push(carg->data, rr->rtype, rr->data, rr->datalen, name, size, rr->ttl);
		}
		else if (rr->rtype == DNS_TYPE_NS) {
			char name[256];
			int size = dns_name_decode_ext(rr->data, metrics, name);
			metric_add_labels4("aggregator_resolve_address", &val, DATATYPE_UINT, carg, "name", carg->data, "data", name, "type", "NS", "class", class);
			dns_record_rule_push(carg->data, rr->rtype, rr->data, rr->datalen, name, size, rr->ttl);
		}
		else if (rr->rtype == DNS_TYPE_PTR) {
			char name[256];
			int size = dns_name_decode_ext(rr->data, metrics, name);
			metric_add_labels4("aggregator_resolve_address", &val, DATATYPE_UINT, carg, "name", carg->data, "data", name, "type", "PTR", "class", class);
			dns_record_rule_push(carg->data, rr->rtype, rr->data, rr->datalen, name, size, rr->ttl);
		}
		else if (rr->rtype == DNS_TYPE_MX) {
			char name[256];
			uint16_t priority_int = rr->data[1];
			char priority[4];
			snprintf(priority, 3, "%hu", priority_int);
			int size = dns_name_decode_ext(rr->data + 2, metrics, name);
			metric_add_labels5("aggregator_resolve_address", &val, DATATYPE_UINT, carg, "name", carg->data, "data", name, "type", "MX", "class", class, "priority", priority);
			dns_record_rule_push(carg->data, rr->rtype, rr->data, rr->datalen, name, size, rr->ttl);
		}
		else if (rr->rtype == DNS_TYPE_SOA) {
			char nsname[256];
			char mxname[256];
			uint8_t *data = (uint8_t*)rr->data;
			int size = 0;
			int64_t serial = 0;
			int64_t refresh = 0;
			int64_t retry = 0;
			int64_t expire = 0;
			int64_t minimum = 0;

			size = dns_name_decode_ext((char*)data, metrics, nsname);
			//printf("NSname %s\n", nsname);

			data = data + size;
			size = dns_name_decode_ext((char*)data, metrics, mxname);
			//printf("MX name %s\n", mxname);

			data = data + size;
			serial = (data[0] * pow(2, 24)) + (data[1] * pow(2, 16)) + (data[2] * pow(2, 8)) + data[3];
			//printf("serial %"d64"\n", serial);

			data = data + 4;
			refresh = (data[0] * pow(2, 24)) + (data[1] * pow(2, 16)) + (data[2] * pow(2, 8)) + data[3];
			//printf("refresh %"d64"\n", refresh);

			data = data + 4;
			retry = (data[0] * pow(2, 24)) + (data[1] * pow(2, 16)) + (data[2] * 256) + data[3];
			//printf("retry %"d64"\n", retry);

			data = data + 4;
			expire = (data[0] * pow(2, 24)) + (data[1] * pow(2, 16)) + (data[2] * pow(2, 8)) + data[3];
			//printf("expire %"d64"\n", expire);

			data = data + 4;
			minimum = (data[0] * pow(2, 24)) + (data[1] * pow(2, 16)) + (data[2] * pow(2, 8)) + data[3];
			//printf("minimum %"d64"\n", minimum);

			metric_add_labels5("aggregator_resolve_soa_serial", &serial, DATATYPE_INT, carg, "name", carg->data, "nameserver", nsname, "mailserver", mxname, "type", "SOA", "class", class);
			metric_add_labels5("aggregator_resolve_soa_refresh", &refresh, DATATYPE_INT, carg, "name", carg->data, "nameserver", nsname, "mailserver", mxname, "type", "SOA", "class", class);
			metric_add_labels5("aggregator_resolve_soa_retry", &retry, DATATYPE_INT, carg, "name", carg->data, "nameserver", nsname, "mailserver", mxname, "type", "SOA", "class", class);
			metric_add_labels5("aggregator_resolve_soa_expire", &expire, DATATYPE_INT, carg, "name", carg->data, "nameserver", nsname, "mailserver", mxname, "type", "SOA", "class", class);
			metric_add_labels5("aggregator_resolve_soa_minimum", &minimum, DATATYPE_INT, carg, "name", carg->data, "nameserver", nsname, "mailserver", mxname, "type", "SOA", "class", class);
		}
		else if (rr->rtype == DNS_TYPE_TXT) {
			char name[256];
			int size = dns_name_decode_ext(rr->data, metrics, name);
			metric_add_labels4("aggregator_resolve_address", &val, DATATYPE_UINT, carg, "name", carg->data, "data", name, "type", "TXT", "class", class);
			dns_record_rule_push(carg->data, rr->rtype, rr->data, rr->datalen, name, size, rr->ttl);
		}
		else if (rr->rtype == DNS_TYPE_SRV) {
			char name[256];
			int size = dns_name_decode_ext(rr->data, metrics, name);
			metric_add_labels4("aggregator_resolve_address", &val, DATATYPE_UINT, carg, "name", carg->data, "data", name, "type", "SRV", "class", class);
			dns_record_rule_push(carg->data, rr->rtype, rr->data, rr->datalen, name, size, rr->ttl);
		}
	}

	metric_add_labels3("aggregator_resolve_address_rr_count", &addr_cnt, DATATYPE_UINT, carg, "name", carg->data, "class", "IN", "type", carg->rrtype);

    	dns_free(&resp);
	carg->parsed = 1;
	return rr_ttl;
}
