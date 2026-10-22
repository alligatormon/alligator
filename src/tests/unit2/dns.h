#include <string.h>
#include <arpa/inet.h>
#include "main.h"
#include "resolver/dns.h"
#include "events/uv_alloc.h"
#include "resolver/resolver.h"
#include "events/context_arg.h"
#include "common/stop.h"

static void test_dns_name_encode_decode(void)
{
	char wire[256];
	char name[DNS_NAME_MAXLEN];

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 12,
		dns_name_encode("google.com", wire));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 12,
		dns_name_decode(wire, name));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "google.com", name);

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 6,
		dns_name_encode("a.co", wire));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 6,
		dns_name_decode(wire, name));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "a.co", name);
}

static void test_dns_name_decode_compression(void)
{
	unsigned char pkt[] = {
		0x12, 0x34, 0x81, 0x80, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
		0x06, 'g','o','o','g','l','e', 0x03, 'c','o','m', 0x00,
		0x00, 0x01, 0x00, 0x01,
		0xc0, 0x0c,
		0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01, 0x2c, 0x00, 0x04,
		0x8e, 0xfb, 0x38, 0x60
	};
	dns_t resp;
	int nparse = dns_unpack((char*)pkt, (int)sizeof(pkt), &resp);

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, (int)sizeof(pkt), nparse);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0x1234, resp.hdr.transaction_id);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, DNS_RESPONSE, resp.hdr.qr);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, resp.hdr.nquestion);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, resp.hdr.nanswer);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "google.com", resp.questions[0].name);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "google.com", resp.answers[0].name);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, DNS_TYPE_A, resp.answers[0].rtype);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 4, resp.answers[0].datalen);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0x8e, (uint8_t)resp.answers[0].data[0]);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0xfb, (uint8_t)resp.answers[0].data[1]);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0x38, (uint8_t)resp.answers[0].data[2]);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0x60, (uint8_t)resp.answers[0].data[3]);

	dns_free(&resp);
}

static void test_dns_query_pack_unpack(void)
{
	dns_t query;
	char buf[512];
	dns_t parsed;

	memset(&query, 0, sizeof(query));
	query.hdr.transaction_id = 42;
	query.hdr.qr = DNS_QUERY;
	query.hdr.rd = 1;
	query.hdr.nquestion = 1;
	query.questions = calloc(1, sizeof(dns_rr_t));
	strlcpy(query.questions[0].name, "yandex.ru", DNS_NAME_MAXLEN);
	query.questions[0].rtype = DNS_TYPE_A;
	query.questions[0].rclass = DNS_CLASS_IN;

	int packed = dns_pack(&query, buf, sizeof(buf));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, packed > (int)sizeof(dnshdr_t));

	int nparse = dns_unpack(buf, packed, &parsed);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, packed, nparse);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 42, parsed.hdr.transaction_id);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, DNS_QUERY, parsed.hdr.qr);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, parsed.hdr.rd);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, parsed.hdr.nquestion);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, parsed.hdr.nanswer);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "yandex.ru", parsed.questions[0].name);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, DNS_TYPE_A, parsed.questions[0].rtype);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, DNS_CLASS_IN, parsed.questions[0].rclass);

	dns_free(&parsed);
	free(query.questions);
}

static void test_dns_init_type_and_strtype(void)
{
	char buf[1024];
	uint16_t tid;
	uint64_t qlen;

	ac->ping_id = 100;
	qlen = dns_init_type("yahoo.com", buf, DNS_TYPE_A, &tid);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, qlen > (uint64_t)sizeof(dnshdr_t));

	dns_t parsed;
	int nparse = dns_unpack(buf, (int)qlen, &parsed);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, (int)qlen, nparse);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, tid, parsed.hdr.transaction_id);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, DNS_QUERY, parsed.hdr.qr);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "yahoo.com", parsed.questions[0].name);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, DNS_TYPE_A, parsed.questions[0].rtype);
	dns_free(&parsed);

	qlen = dns_init_strtype("example.com", buf, "aaaa", &tid);
	nparse = dns_unpack(buf, (int)qlen, &parsed);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, (int)qlen, nparse);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, DNS_TYPE_AAAA, parsed.questions[0].rtype);
	dns_free(&parsed);

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, DNS_TYPE_A, get_rrtype_by_str("a"));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, DNS_TYPE_AAAA, get_rrtype_by_str("AAAA"));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, get_rrtype_by_str("unknown"));
}

static void test_dns_response_pack_roundtrip(void)
{
	dns_t resp;
	char buf[512];
	dns_t parsed;
	uint8_t ip[4] = {81, 19, 83, 16};

	memset(&resp, 0, sizeof(resp));
	resp.hdr.transaction_id = 0xbeef;
	resp.hdr.qr = DNS_RESPONSE;
	resp.hdr.ra = 1;
	resp.hdr.rd = 1;
	resp.hdr.nquestion = 1;
	resp.hdr.nanswer = 1;
	resp.questions = calloc(1, sizeof(dns_rr_t));
	resp.answers = calloc(1, sizeof(dns_rr_t));

	strlcpy(resp.questions[0].name, "yahoo.com", DNS_NAME_MAXLEN);
	resp.questions[0].rtype = DNS_TYPE_A;
	resp.questions[0].rclass = DNS_CLASS_IN;

	strlcpy(resp.answers[0].name, "yahoo.com", DNS_NAME_MAXLEN);
	resp.answers[0].rtype = DNS_TYPE_A;
	resp.answers[0].rclass = DNS_CLASS_IN;
	resp.answers[0].ttl = 300;
	resp.answers[0].datalen = 4;
	resp.answers[0].data = (char*)ip;

	int packed = dns_pack(&resp, buf, sizeof(buf));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, packed > 0);

	int nparse = dns_unpack(buf, packed, &parsed);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, packed, nparse);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0xbeef, parsed.hdr.transaction_id);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, DNS_RESPONSE, parsed.hdr.qr);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "yahoo.com", parsed.questions[0].name);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "yahoo.com", parsed.answers[0].name);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 300, (int)parsed.answers[0].ttl);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
		memcmp(parsed.answers[0].data, ip, 4));

	dns_free(&parsed);
	free(resp.questions);
	free(resp.answers);
}

static void test_resolver_udp_match_pending_txid_and_qname(void)
{
	alligator_ht *pending = alligator_ht_init(NULL);
	context_arg google;
	context_arg yandex;
	unsigned char google_pkt[] = {
		0x12, 0x34, 0x81, 0x80, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
		0x06, 'g','o','o','g','l','e', 0x03, 'c','o','m', 0x00,
		0x00, 0x01, 0x00, 0x01,
		0xc0, 0x0c,
		0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01, 0x2c, 0x00, 0x04,
		0x8e, 0xfb, 0x38, 0x60
	};
	unsigned char yandex_pkt[] = {
		0x22, 0x22, 0x81, 0x80, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
		0x06, 'y','a','n','d','e','x', 0x02, 'r','u', 0x00,
		0x00, 0x01, 0x00, 0x01,
		0xc0, 0x0c,
		0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01, 0x2c, 0x00, 0x04,
		0x4d, 0x4d, 0x00, 0x01
	};
	unsigned char google_qname_only[] = {
		0x00, 0x01, 0x81, 0x80, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
		0x06, 'g','o','o','g','l','e', 0x03, 'c','o','m', 0x00,
		0x00, 0x01, 0x00, 0x01,
		0xc0, 0x0c,
		0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01, 0x2c, 0x00, 0x04,
		0x8e, 0xfb, 0x38, 0x60
	};

	memset(&google, 0, sizeof(google));
	memset(&yandex, 0, sizeof(yandex));
	google.packets_id = 0x1234;
	google.data = "google.com";
	google.key = "udp://8.8.8.8/google.com:IN:a";
	yandex.packets_id = 0x2222;
	yandex.data = "yandex.ru";
	yandex.key = "udp://8.8.8.8/yandex.ru:IN:a";

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		resolver_udp_pending_add(pending, &google));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		resolver_udp_pending_add(pending, &yandex));

	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__,
		resolver_udp_match_pending(pending, (char *)google_pkt, sizeof(google_pkt)));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		resolver_udp_match_pending(pending, (char *)google_pkt, sizeof(google_pkt)) == &google);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		resolver_udp_match_pending(pending, (char *)yandex_pkt, sizeof(yandex_pkt)) == &yandex);

	/* Unknown txid still maps by QNAME from the question section. */
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		resolver_udp_match_pending(pending, (char *)google_qname_only, sizeof(google_qname_only)) == &google);

	resolver_udp_pending_del(pending, &google);
	resolver_udp_pending_del(pending, &yandex);
	alligator_ht_done(pending);
	free(pending);
}

static void test_alloc_buffer_ignores_non_carg_handle_data(void)
{
	unsigned char blob[8192];
	uv_handle_t handle;
	uv_buf_t buf;

	/* Shared DNS binds store resolver_udp_bind* in handle->data. alloc_buffer
	 * used to treat that as context_arg and bzero a garbage uvbuf pointer,
	 * which zeroed recv_cb (SIGSEGV at 0x0 in uv__udp_recvmsg). */
	memset(blob, 0xff, sizeof(blob));
	memset(&handle, 0, sizeof(handle));
	handle.data = blob;
	alloc_buffer(&handle, 128, &buf);
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, buf.base);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 128, (int)buf.len);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, buf.base == (char *)blob);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, (unsigned char)buf.base[0]);
	free(buf.base);
}

static void test_dns_handler_parse_response(void)
{
	unsigned char pkt[] = {
		0x12, 0x34, 0x81, 0x80, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
		0x06, 'g','o','o','g','l','e', 0x03, 'c','o','m', 0x00,
		0x00, 0x01, 0x00, 0x01,
		0xc0, 0x0c,
		0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01, 0x2c, 0x00, 0x04,
		0x4a, 0x7d, 0xcd, 0x64
	};
	/* MX response for example.com with two answers (prio 10 and 5 → mx1.example.com) */
	unsigned char mx_pkt[] = {
		0x12, 0x34, 0x81, 0x80, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
		0x07, 'e','x','a','m','p','l','e', 0x03, 'c','o','m', 0x00,
		0x00, 0x0f, 0x00, 0x01,
		/* answer 1: MX priority 10 mx1.example.com */
		0xc0, 0x0c,
		0x00, 0x0f, 0x00, 0x01, 0x00, 0x00, 0x01, 0x2c, 0x00, 0x13,
		0x00, 0x0a,
		0x03, 'm','x','1', 0x07, 'e','x','a','m','p','l','e', 0x03, 'c','o','m', 0x00,
		/* answer 2: MX priority 5, compress exchange to answer1 RDATA name (offset 43) */
		0xc0, 0x0c,
		0x00, 0x0f, 0x00, 0x01, 0x00, 0x00, 0x01, 0x2c, 0x00, 0x04,
		0x00, 0x05,
		0xc0, 0x2b
	};
	context_arg carg;
	uint64_t ttl;

	if (!ac->resolver)
		ac->resolver = alligator_ht_init(NULL);

	memset(&carg, 0, sizeof(carg));
	carg.ttl = -1;
	carg.curr_ttl = -1;
	carg.packets_id = 0x1234;
	carg.rrtype = "A";
	carg.key = "udp://127.0.0.1:53/google.com:IN:1";
	strlcpy(carg.host, "127.0.0.1", HOSTHEADER_SIZE);

	ttl = dns_handler((char*)pkt, sizeof(pkt), &carg);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg.parsed);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 300, (int)ttl);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 300, (int)carg.curr_ttl);
	metric_test_run(CMP_EQUAL,
		"alligator_dns_rr_count{name=\"google.com\",type=\"A\",class=\"IN\"}",
		"alligator_dns_rr_count", 1);
	metric_test_run(CMP_EQUAL,
		"alligator_dns_rr_info{name=\"google.com\",type=\"A\",data=\"74.125.205.100\"}",
		"alligator_dns_rr_info", 1);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg.dns_status_reported);
	metric_test_run(CMP_EQUAL,
		"alligator_dns_resolution_status{name=\"google.com\",type=\"A\",class=\"IN\"}",
		"alligator_dns_resolution_status", 1);

	carg.resolver = 1;
	carg.parsed = 0;
	carg.curr_ttl = -1;
	ttl = dns_handler((char*)pkt, sizeof(pkt), &carg);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg.parsed);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 30, (int)carg.curr_ttl);

	/* MX: rr_count must match successfully emitted rr_info rows (not stay 0). */
	memset(&carg, 0, sizeof(carg));
	carg.ttl = -1;
	carg.curr_ttl = -1;
	carg.packets_id = 0x1234;
	carg.rrtype = "MX";
	carg.key = "udp://127.0.0.1:53/example.com:IN:15";
	strlcpy(carg.host, "127.0.0.1", HOSTHEADER_SIZE);

	ttl = dns_handler((char*)mx_pkt, sizeof(mx_pkt), &carg);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg.parsed);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 300, (int)ttl);
	metric_test_run(CMP_EQUAL,
		"alligator_dns_rr_count{name=\"example.com\",type=\"MX\",class=\"IN\"}",
		"alligator_dns_rr_count", 2);
	metric_test_run(CMP_EQUAL,
		"alligator_dns_rr_info{name=\"example.com\",type=\"MX\",data=\"mx1.example.com\",priority=\"10\"}",
		"alligator_dns_rr_info", 1);
	metric_test_run(CMP_EQUAL,
		"alligator_dns_rr_info{name=\"example.com\",type=\"MX\",data=\"mx1.example.com\",priority=\"5\"}",
		"alligator_dns_rr_info", 1);
	/* Generic types count as resolved even without A/AAAA answers. */
	metric_test_run(CMP_EQUAL,
		"alligator_dns_resolution_status{name=\"example.com\",type=\"MX\",class=\"IN\"}",
		"alligator_dns_resolution_status", 1);
	metric_test_run(CMP_EQUAL,
		"alligator_dns_resolutions_total{name=\"example.com\",type=\"MX\",class=\"IN\",result=\"success\"}",
		"alligator_dns_resolutions_total", 1);

	/* Empty answer: the target never reaches the parser, so the DNS-layer
	 * status must report the failure on its own. */
	{
		unsigned char empty_pkt[] = {
			0x12, 0x34, 0x81, 0x80, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x05, 'e','m','p','t','y', 0x04, 't','e','s','t', 0x00,
			0x00, 0x01, 0x00, 0x01
		};
		memset(&carg, 0, sizeof(carg));
		carg.ttl = -1;
		carg.curr_ttl = -1;
		carg.packets_id = 0x1234;
		carg.rrtype = "A";
		carg.data = "empty.test";
		carg.key = "udp://127.0.0.1:53/empty.test:IN:1";
		strlcpy(carg.host, "127.0.0.1", HOSTHEADER_SIZE);

		ttl = dns_handler((char*)empty_pkt, sizeof(empty_pkt), &carg);
		assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, carg.parsed);
		assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, carg.dns_usable);
		assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg.dns_status_reported);
		metric_test_run(CMP_EQUAL,
			"alligator_dns_resolution_status{name=\"empty.test\",type=\"A\",class=\"IN\"}",
			"alligator_dns_resolution_status", 0);
		metric_test_run(CMP_EQUAL,
			"alligator_dns_resolutions_total{name=\"empty.test\",type=\"A\",class=\"IN\",result=\"empty\"}",
			"alligator_dns_resolutions_total", 1);
		(void)ttl;
	}
}

static void test_dns_unpack_invalid(void)
{
	char buf[4] = {0};
	char domain[DNS_NAME_MAXLEN];
	unsigned char wire[2] = {0xc0, 0xff};
	dns_t parsed;

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, -1, dns_unpack(buf, 4, &parsed));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, -1,
		dns_name_decode_msg((char*)wire, (char*)wire, domain));

	/* 130 one-byte labels: presentation form does not fit DNS_NAME_MAXLEN */
	unsigned char longname[261];
	int li;
	for (li = 0; li < 130; li++) {
		longname[li * 2] = 1;
		longname[li * 2 + 1] = 'a';
	}
	longname[260] = 0;
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, -1,
		dns_name_decode((char*)longname, domain));
}

/* Declared in vrl/type.h; prototype here to avoid pulling the full VRL/uv
 * header stack into the DNS test unit. */
int vrl_dns_reverse_name(const char *ip, char *out, size_t outlen);

static void test_vrl_dns_reverse_name(void)
{
	char out[DNS_NAME_MAXLEN];

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		vrl_dns_reverse_name("1.2.3.4", out, sizeof(out)));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__,
		"4.3.2.1.in-addr.arpa", out);

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		vrl_dns_reverse_name("203.0.113.7", out, sizeof(out)));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__,
		"7.113.0.203.in-addr.arpa", out);

	/* IPv6 nibble-reversed: 32 nibbles ("N.") + "ip6.arpa" = 72 chars.
	 * ::1 has its single set nibble first. */
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		vrl_dns_reverse_name("::1", out, sizeof(out)));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 72, (int)strlen(out));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
		strncmp(out, "1.0.0.0.", 8));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
		strcmp(out + strlen(out) - 8, "ip6.arpa"));

	/* Invalid literals are rejected. */
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
		vrl_dns_reverse_name("not-an-ip", out, sizeof(out)));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
		vrl_dns_reverse_name("999.1.1.1", out, sizeof(out)));
}

static void test_resolver_cache_lookup(void)
{
	if (!ac->resolver)
		ac->resolver = alligator_ht_init(NULL);

	/* Miss: no probe is started, just NULL. */
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		resolver_cache_lookup("absent.example.org", DNS_TYPE_A) == NULL);

	/* Forward hit after a store (same path dns_lookup uses). */
	dns_record_rule_push("cache.example.org", DNS_TYPE_A, NULL, 0,
		"203.0.113.7", strlen("203.0.113.7"), 300);
	string *v = resolver_cache_lookup("cache.example.org", DNS_TYPE_A);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, v != NULL);
	if (v)
		assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "203.0.113.7", v->s);

	/* Reverse (PTR) hit under the in-addr.arpa key. */
	dns_record_rule_push("7.113.0.203.in-addr.arpa", DNS_TYPE_PTR, NULL, 0,
		"host.example.org", strlen("host.example.org"), 300);
	string *p = resolver_cache_lookup("7.113.0.203.in-addr.arpa", DNS_TYPE_PTR);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, p != NULL);
	if (p)
		assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "host.example.org", p->s);

	/* Expired record is not returned (ttl in the past). */
	dns_record_rule_push("expired.example.org", DNS_TYPE_A, NULL, 0,
		"198.51.100.9", strlen("198.51.100.9"), 0);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		resolver_cache_lookup("expired.example.org", DNS_TYPE_A) == NULL);
}

/* Failed resolver retry must not keep a context_arg per attempt. The timer is
 * closed before uv_run, so this never sends a packet to 192.0.2.1. */
static void test_resolver_drain_closes(void)
{
	unsigned i;

	for (i = 0; i < 32; ++i) {
		if (!uv_run(ac->loop, UV_RUN_NOWAIT))
			break;
	}
}

static void test_resolver_ephemeral_retry_frees(void)
{
	resolver_data *saved_slots[255];
	uint8_t saved_size;
	alligator_ht *saved_ag;
	alligator_ht *tmp_ag = NULL;
	json_t *js;
	char url[] = "http://brand.example/";
	host_aggregator_info *hi;
	context_arg *client;
	context_arg *probe;
	const char *name = "ephemeral.example";
	resolver_data *rd;

	if (!ac || !ac->loop)
		return;

	saved_size = ac->resolver_size;
	memcpy(saved_slots, ac->srv_resolver, sizeof(saved_slots));
	memset(ac->srv_resolver, 0, sizeof(ac->srv_resolver));
	ac->resolver_size = 0;
	if (!ac->resolver)
		ac->resolver = alligator_ht_init(NULL);

	js = json_string("udp://192.0.2.1:53");
	resolver_push_json(js);
	json_decref(js);

	hi = parse_url(url, strlen(url));
	client = hi ? context_arg_json_fill(NULL, hi, NULL, "blackbox", NULL, 0, NULL, NULL, 0, ac->loop, NULL, 0, NULL, 0) : NULL;
	url_free(hi);
	if (!client || !ac->resolver_size) {
		if (client)
			carg_free(client);
		goto restore;
	}

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		aggregator_get_addr(client, (char *)name, DNS_TYPE_A, DNS_CLASS_IN) == NULL);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, (int)resolver_ephemeral_count());
	/* Second miss while the first probe is in flight must not allocate. */
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		aggregator_get_addr(client, (char *)name, DNS_TYPE_A, DNS_CLASS_IN) == NULL);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, (int)resolver_ephemeral_count());

	probe = resolver_ephemeral_lookup(name, DNS_TYPE_A);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, probe != NULL);
	if (probe) {
		saved_ag = ac->aggregators;
		if (!ac->aggregators) {
			tmp_ag = alligator_ht_init(NULL);
			ac->aggregators = tmp_ag;
		}
		alligator_ht_insert(ac->aggregators, &probe->context_node, probe, tommy_strhash_u32(0, probe->key));
		/* Registered permanent-style entry must survive a failed-attempt release. */
		resolver_ephemeral_release(probe);
		assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, (int)resolver_ephemeral_count());
		assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
			resolver_ephemeral_lookup(name, DNS_TYPE_A) == probe);
		alligator_ht_remove_existing(ac->aggregators, &probe->context_node);
		ac->aggregators = saved_ag;

		resolver_ephemeral_release(probe);
		/* Slot stays until close_cbs finish so a concurrent crawl cannot overlap. */
		assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, (int)resolver_ephemeral_count());
		assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
			aggregator_get_addr(client, (char *)name, DNS_TYPE_A, DNS_CLASS_IN) == NULL);
		assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, (int)resolver_ephemeral_count());
		test_resolver_drain_closes();
		assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, (int)resolver_ephemeral_count());
	}

	/* Crawl retry allocates one new probe, then that attempt is freed too. */
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		aggregator_get_addr(client, (char *)name, DNS_TYPE_A, DNS_CLASS_IN) == NULL);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, (int)resolver_ephemeral_count());
	probe = resolver_ephemeral_lookup(name, DNS_TYPE_A);
	if (probe)
		resolver_ephemeral_release(probe);
	test_resolver_drain_closes();
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, (int)resolver_ephemeral_count());
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "brand.example", client->host);
	carg_free(client);

restore:
	if (ac->resolver_size) {
		rd = ac->srv_resolver[0];
		ac->srv_resolver[0] = NULL;
		ac->resolver_size = 0;
		if (rd) {
			url_free(rd->hi);
			labels_hash_free(rd->labels);
			free_percentile_buffer(rd->response_time);
			free_percentile_buffer(rd->read_time);
			free_percentile_buffer(rd->write_time);
			free(rd);
		}
	}
	memcpy(ac->srv_resolver, saved_slots, sizeof(saved_slots));
	ac->resolver_size = saved_size;
	if (tmp_ag) {
		alligator_ht_done(tmp_ag);
		pthread_rwlock_destroy(&tmp_ag->rwlock);
		free(tmp_ag);
	}
}

/* TCP write/read sync failures call resolver_ephemeral_release (cannot drive
 * uv_write failure without a live peer). Assert that same path unsticks the
 * name so aggregator_get_addr can allocate a fresh probe. */
static void test_resolver_ephemeral_tcp_write_fail_unsticks(void)
{
	resolver_data *saved_slots[255];
	uint8_t saved_size;
	json_t *js;
	char url[] = "http://brand.example/";
	host_aggregator_info *hi;
	context_arg *client;
	context_arg *probe;
	const char *name = "ephemeral-tcp.example";
	resolver_data *rd;

	if (!ac || !ac->loop)
		return;

	saved_size = ac->resolver_size;
	memcpy(saved_slots, ac->srv_resolver, sizeof(saved_slots));
	memset(ac->srv_resolver, 0, sizeof(ac->srv_resolver));
	ac->resolver_size = 0;
	if (!ac->resolver)
		ac->resolver = alligator_ht_init(NULL);

	js = json_string("tcp://192.0.2.1:53");
	resolver_push_json(js);
	json_decref(js);

	hi = parse_url(url, strlen(url));
	client = hi ? context_arg_json_fill(NULL, hi, NULL, "blackbox", NULL, 0, NULL, NULL, 0, ac->loop, NULL, 0, NULL, 0) : NULL;
	url_free(hi);
	if (!client || !ac->resolver_size) {
		if (client)
			carg_free(client);
		goto restore;
	}

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		aggregator_get_addr(client, (char *)name, DNS_TYPE_A, DNS_CLASS_IN) == NULL);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, (int)resolver_ephemeral_count());
	probe = resolver_ephemeral_lookup(name, DNS_TYPE_A);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, probe != NULL);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		probe && (probe->transport == APROTO_TCP || probe->proto == APROTO_TCP));

	/* Same cleanup as resolver_writed_tcp / resolver_connected_tcp failures. */
	if (probe)
		resolver_ephemeral_release(probe);
	test_resolver_drain_closes();
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, (int)resolver_ephemeral_count());
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		resolver_ephemeral_lookup(name, DNS_TYPE_A) == NULL);

	/* Name must be lookupable again (not stuck in-flight). */
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		aggregator_get_addr(client, (char *)name, DNS_TYPE_A, DNS_CLASS_IN) == NULL);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, (int)resolver_ephemeral_count());
	probe = resolver_ephemeral_lookup(name, DNS_TYPE_A);
	if (probe)
		resolver_ephemeral_release(probe);
	test_resolver_drain_closes();
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, (int)resolver_ephemeral_count());
	carg_free(client);

restore:
	if (ac->resolver_size) {
		rd = ac->srv_resolver[0];
		ac->srv_resolver[0] = NULL;
		ac->resolver_size = 0;
		if (rd) {
			url_free(rd->hi);
			labels_hash_free(rd->labels);
			free_percentile_buffer(rd->response_time);
			free_percentile_buffer(rd->read_time);
			free_percentile_buffer(rd->write_time);
			free(rd);
		}
	}
	memcpy(ac->srv_resolver, saved_slots, sizeof(saved_slots));
	ac->resolver_size = saved_size;
}

/* Permanent dns TCP aggregates must uv_close after sync connect failures so the
 * repeating resolver_timer can safely init a new handle on the next period. */
static void test_resolver_tcp_permanent_fail_closes(void)
{
	resolver_data *saved_slots[255];
	uint8_t saved_size;
	json_t *js;
	char url[] = "tcp://192.0.2.1:53";
	host_aggregator_info *hi;
	context_arg *seed;
	context_arg *probe;
	resolver_data *rd;

	if (!ac || !ac->loop)
		return;

	saved_size = ac->resolver_size;
	memcpy(saved_slots, ac->srv_resolver, sizeof(saved_slots));
	memset(ac->srv_resolver, 0, sizeof(ac->srv_resolver));
	ac->resolver_size = 0;
	if (!ac->resolver)
		ac->resolver = alligator_ht_init(NULL);

	js = json_string("tcp://192.0.2.1:53");
	resolver_push_json(js);
	json_decref(js);

	hi = parse_url(url, strlen(url));
	seed = hi ? context_arg_json_fill(NULL, hi, dns_handler, "dns_handler", NULL, 0,
		strdup("permanent-tcp.example"), NULL, 0, ac->loop, NULL, 1, NULL, 0) : NULL;
	url_free(hi);
	if (!seed || !ac->resolver_size) {
		if (seed)
			carg_free(seed);
		goto restore;
	}
	seed->rrtype = "A";
	seed->rrclass = DNS_CLASS_IN;
	seed->resolver = 1;

	probe = aggregator_push_addr_strtype(seed, seed->data, seed->rrtype, seed->rrclass);
	free(seed->data);
	seed->data = NULL;
	carg_free(seed);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, probe != NULL);
	if (!probe)
		goto restore;
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, probe->resolver_ephemeral);

	/* Stop the repeating kickoff; simulate a post-uv_tcp_init sync failure. */
	uv_timer_stop(&probe->resolver_timer);
	memset(&probe->client, 0, sizeof(probe->client));
	probe->client.data = probe;
	uv_tcp_init(ac->loop, &probe->client);
	probe->lock = 1;
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, resolver_tcp_halt(probe));
	test_resolver_drain_closes();
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, probe->lock);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
		probe->client.loop && probe->client.type != UV_UNKNOWN_HANDLE &&
		!uv_is_closing((uv_handle_t *)&probe->client));

	if (probe->resolver_timer.loop &&
	    !uv_is_closing((uv_handle_t *)&probe->resolver_timer)) {
		uv_timer_stop(&probe->resolver_timer);
		probe->resolver_timer.data = NULL;
		uv_close((uv_handle_t *)&probe->resolver_timer, resolver_timer_handle_closed);
	}
	test_resolver_drain_closes();
	free(probe->data);
	probe->data = NULL;
	carg_free(probe);

restore:
	if (ac->resolver_size) {
		rd = ac->srv_resolver[0];
		ac->srv_resolver[0] = NULL;
		ac->resolver_size = 0;
		if (rd) {
			url_free(rd->hi);
			labels_hash_free(rd->labels);
			free_percentile_buffer(rd->response_time);
			free_percentile_buffer(rd->read_time);
			free_percentile_buffer(rd->write_time);
			free(rd);
		}
	}
	memcpy(ac->srv_resolver, saved_slots, sizeof(saved_slots));
	ac->resolver_size = saved_size;
}

/* Permanent TCP success must halt (not only lock=0). Re-entry with a live
 * client and lock==0 must close, never memset/uv_tcp_init over the handle. */
static void test_resolver_tcp_permanent_success_halts(void)
{
	resolver_data *saved_slots[255];
	uint8_t saved_size;
	json_t *js;
	char url[] = "tcp://192.0.2.1:53";
	host_aggregator_info *hi;
	context_arg *seed;
	context_arg *probe;
	resolver_data *rd;

	if (!ac || !ac->loop)
		return;

	saved_size = ac->resolver_size;
	memcpy(saved_slots, ac->srv_resolver, sizeof(saved_slots));
	memset(ac->srv_resolver, 0, sizeof(ac->srv_resolver));
	ac->resolver_size = 0;
	if (!ac->resolver)
		ac->resolver = alligator_ht_init(NULL);

	js = json_string("tcp://192.0.2.1:53");
	resolver_push_json(js);
	json_decref(js);

	hi = parse_url(url, strlen(url));
	seed = hi ? context_arg_json_fill(NULL, hi, dns_handler, "dns_handler", NULL, 0,
		strdup("permanent-tcp-ok.example"), NULL, 0, ac->loop, NULL, 1, NULL, 0) : NULL;
	url_free(hi);
	if (!seed || !ac->resolver_size) {
		if (seed)
			carg_free(seed);
		goto restore;
	}
	seed->rrtype = "A";
	seed->rrclass = DNS_CLASS_IN;
	seed->resolver = 1;

	probe = aggregator_push_addr_strtype(seed, seed->data, seed->rrtype, seed->rrclass);
	free(seed->data);
	seed->data = NULL;
	carg_free(seed);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, probe != NULL);
	if (!probe)
		goto restore;
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, probe->resolver_ephemeral);

	uv_timer_stop(&probe->resolver_timer);
	memset(&probe->client, 0, sizeof(probe->client));
	probe->client.data = probe;
	uv_tcp_init(ac->loop, &probe->client);
	/* Buggy success path: lock cleared while client still live. */
	probe->lock = 0;
	resolver_connect_tcp(probe);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		uv_is_closing((uv_handle_t *)&probe->client) ||
		!(probe->client.loop && probe->client.type != UV_UNKNOWN_HANDLE));
	test_resolver_drain_closes();
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, probe->lock);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
		probe->client.loop && probe->client.type != UV_UNKNOWN_HANDLE &&
		!uv_is_closing((uv_handle_t *)&probe->client));

	if (probe->resolver_timer.loop &&
	    !uv_is_closing((uv_handle_t *)&probe->resolver_timer)) {
		uv_timer_stop(&probe->resolver_timer);
		probe->resolver_timer.data = NULL;
		uv_close((uv_handle_t *)&probe->resolver_timer, resolver_timer_handle_closed);
	}
	test_resolver_drain_closes();
	free(probe->data);
	probe->data = NULL;
	carg_free(probe);

restore:
	if (ac->resolver_size) {
		rd = ac->srv_resolver[0];
		ac->srv_resolver[0] = NULL;
		ac->resolver_size = 0;
		if (rd) {
			url_free(rd->hi);
			labels_hash_free(rd->labels);
			free_percentile_buffer(rd->response_time);
			free_percentile_buffer(rd->read_time);
			free_percentile_buffer(rd->write_time);
			free(rd);
		}
	}
	memcpy(ac->srv_resolver, saved_slots, sizeof(saved_slots));
	ac->resolver_size = saved_size;
}

/* connect_tcp re-entry with lock+open client must release ephemeral probes
 * (bare halt would leave them stuck in resolver_ephemeral_ht). */
static void test_resolver_ephemeral_connect_tcp_reentry_releases(void)
{
	resolver_data *saved_slots[255];
	uint8_t saved_size;
	json_t *js;
	char url[] = "http://brand.example/";
	host_aggregator_info *hi;
	context_arg *client;
	context_arg *probe;
	const char *name = "ephemeral-tcp-reentry.example";
	resolver_data *rd;

	if (!ac || !ac->loop)
		return;

	saved_size = ac->resolver_size;
	memcpy(saved_slots, ac->srv_resolver, sizeof(saved_slots));
	memset(ac->srv_resolver, 0, sizeof(ac->srv_resolver));
	ac->resolver_size = 0;
	if (!ac->resolver)
		ac->resolver = alligator_ht_init(NULL);

	js = json_string("tcp://192.0.2.1:53");
	resolver_push_json(js);
	json_decref(js);

	hi = parse_url(url, strlen(url));
	client = hi ? context_arg_json_fill(NULL, hi, NULL, "blackbox", NULL, 0, NULL, NULL, 0, ac->loop, NULL, 0, NULL, 0) : NULL;
	url_free(hi);
	if (!client || !ac->resolver_size) {
		if (client)
			carg_free(client);
		goto restore;
	}

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		aggregator_get_addr(client, (char *)name, DNS_TYPE_A, DNS_CLASS_IN) == NULL);
	probe = resolver_ephemeral_lookup(name, DNS_TYPE_A);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, probe != NULL);
	if (!probe) {
		carg_free(client);
		goto restore;
	}

	/* Simulate prior attempt still owning the TCP handle + lock (no tt_timer). */
	uv_timer_stop(&probe->resolver_timer);
	if (probe->tt_timer) {
		uv_timer_stop(probe->tt_timer);
		probe->tt_timer->data = NULL;
		alligator_cache_push(ac->uv_cache_timer, probe->tt_timer);
		probe->tt_timer = NULL;
	}
	memset(&probe->client, 0, sizeof(probe->client));
	probe->client.data = probe;
	uv_tcp_init(ac->loop, &probe->client);
	probe->lock = 1;

	resolver_connect_tcp(probe);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, probe->resolver_ephemeral_freeing);
	test_resolver_drain_closes();
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, (int)resolver_ephemeral_count());
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		resolver_ephemeral_lookup(name, DNS_TYPE_A) == NULL);
	carg_free(client);

restore:
	if (ac->resolver_size) {
		rd = ac->srv_resolver[0];
		ac->srv_resolver[0] = NULL;
		ac->resolver_size = 0;
		if (rd) {
			url_free(rd->hi);
			labels_hash_free(rd->labels);
			free_percentile_buffer(rd->response_time);
			free_percentile_buffer(rd->read_time);
			free_percentile_buffer(rd->write_time);
			free(rd);
		}
	}
	memcpy(ac->srv_resolver, saved_slots, sizeof(saved_slots));
	ac->resolver_size = saved_size;
}

/* connect_udp re-entry with lock+open dedicated udp_client must release
 * ephemeral probes (bare halt / memset would UAF or stick in the ht). */
static void test_resolver_ephemeral_connect_udp_reentry_releases(void)
{
	resolver_data *saved_slots[255];
	uint8_t saved_size;
	json_t *js;
	char url[] = "http://brand.example/";
	host_aggregator_info *hi;
	context_arg *client;
	context_arg *probe;
	const char *name = "ephemeral-udp-reentry.example";
	resolver_data *rd;

	if (!ac || !ac->loop)
		return;

	saved_size = ac->resolver_size;
	memcpy(saved_slots, ac->srv_resolver, sizeof(saved_slots));
	memset(ac->srv_resolver, 0, sizeof(ac->srv_resolver));
	ac->resolver_size = 0;
	if (!ac->resolver)
		ac->resolver = alligator_ht_init(NULL);

	js = json_string("udp://192.0.2.1:53");
	resolver_push_json(js);
	json_decref(js);

	hi = parse_url(url, strlen(url));
	client = hi ? context_arg_json_fill(NULL, hi, NULL, "blackbox", NULL, 0, NULL, NULL, 0, ac->loop, NULL, 0, NULL, 0) : NULL;
	url_free(hi);
	if (!client || !ac->resolver_size) {
		if (client)
			carg_free(client);
		goto restore;
	}

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		aggregator_get_addr(client, (char *)name, DNS_TYPE_A, DNS_CLASS_IN) == NULL);
	probe = resolver_ephemeral_lookup(name, DNS_TYPE_A);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, probe != NULL);
	if (!probe) {
		carg_free(client);
		goto restore;
	}

	uv_timer_stop(&probe->resolver_timer);
	if (probe->tt_timer) {
		uv_timer_stop(probe->tt_timer);
		probe->tt_timer->data = NULL;
		alligator_cache_push(ac->uv_cache_timer, probe->tt_timer);
		probe->tt_timer = NULL;
	}
	memset(&probe->udp_client, 0, sizeof(probe->udp_client));
	probe->udp_client.data = probe;
	uv_udp_init(ac->loop, &probe->udp_client);
	probe->lock = 1;
	probe->resolver_udp_shared = 0;

	resolver_connect_udp(probe);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, probe->resolver_ephemeral_freeing);
	test_resolver_drain_closes();
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, (int)resolver_ephemeral_count());
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		resolver_ephemeral_lookup(name, DNS_TYPE_A) == NULL);
	carg_free(client);

restore:
	if (ac->resolver_size) {
		rd = ac->srv_resolver[0];
		ac->srv_resolver[0] = NULL;
		ac->resolver_size = 0;
		if (rd) {
			url_free(rd->hi);
			labels_hash_free(rd->labels);
			free_percentile_buffer(rd->response_time);
			free_percentile_buffer(rd->read_time);
			free_percentile_buffer(rd->write_time);
			free(rd);
		}
	}
	memcpy(ac->srv_resolver, saved_slots, sizeof(saved_slots));
	ac->resolver_size = saved_size;
}

/* Release while TCP client is already closing must wait for close_cb (no
 * sync-free / UAF). */
static void test_resolver_ephemeral_release_while_closing(void)
{
	resolver_data *saved_slots[255];
	uint8_t saved_size;
	json_t *js;
	char url[] = "http://brand.example/";
	host_aggregator_info *hi;
	context_arg *client;
	context_arg *probe;
	const char *name = "ephemeral-closing.example";
	resolver_data *rd;

	if (!ac || !ac->loop)
		return;

	saved_size = ac->resolver_size;
	memcpy(saved_slots, ac->srv_resolver, sizeof(saved_slots));
	memset(ac->srv_resolver, 0, sizeof(ac->srv_resolver));
	ac->resolver_size = 0;
	if (!ac->resolver)
		ac->resolver = alligator_ht_init(NULL);

	js = json_string("tcp://192.0.2.1:53");
	resolver_push_json(js);
	json_decref(js);

	hi = parse_url(url, strlen(url));
	client = hi ? context_arg_json_fill(NULL, hi, NULL, "blackbox", NULL, 0, NULL, NULL, 0, ac->loop, NULL, 0, NULL, 0) : NULL;
	url_free(hi);
	if (!client || !ac->resolver_size) {
		if (client)
			carg_free(client);
		goto restore;
	}

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		aggregator_get_addr(client, (char *)name, DNS_TYPE_A, DNS_CLASS_IN) == NULL);
	probe = resolver_ephemeral_lookup(name, DNS_TYPE_A);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, probe != NULL);
	if (!probe) {
		carg_free(client);
		goto restore;
	}

	uv_timer_stop(&probe->resolver_timer);
	memset(&probe->client, 0, sizeof(probe->client));
	probe->client.data = probe;
	uv_tcp_init(ac->loop, &probe->client);
	/* Bare halt starts close with resolver_closed_tcp; release must count the
	 * already-closing client and wait (not sync-free). */
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, resolver_tcp_halt(probe));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		uv_is_closing((uv_handle_t *)&probe->client));

	resolver_ephemeral_release(probe);
	/* Must not have sync-freed: still tracked until close_cbs finish. */
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, (int)resolver_ephemeral_count());
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, probe->resolver_ephemeral_freeing);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, probe->resolver_ephemeral_closes >= 1);
	test_resolver_drain_closes();
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, (int)resolver_ephemeral_count());
	carg_free(client);

restore:
	if (ac->resolver_size) {
		rd = ac->srv_resolver[0];
		ac->srv_resolver[0] = NULL;
		ac->resolver_size = 0;
		if (rd) {
			url_free(rd->hi);
			labels_hash_free(rd->labels);
			free_percentile_buffer(rd->response_time);
			free_percentile_buffer(rd->read_time);
			free_percentile_buffer(rd->write_time);
			free(rd);
		}
	}
	memcpy(ac->srv_resolver, saved_slots, sizeof(saved_slots));
	ac->resolver_size = saved_size;
}

/* After TCP close_cb finishes, client must be inert: halt returns 0 and release
 * must not wait on a phantom close (would stick forever in ephemeral ht). */
static void test_resolver_ephemeral_release_after_tcp_closed(void)
{
	resolver_data *saved_slots[255];
	uint8_t saved_size;
	json_t *js;
	char url[] = "http://brand.example/";
	host_aggregator_info *hi;
	context_arg *client;
	context_arg *probe;
	const char *name = "ephemeral-after-closed.example";
	resolver_data *rd;

	if (!ac || !ac->loop)
		return;

	saved_size = ac->resolver_size;
	memcpy(saved_slots, ac->srv_resolver, sizeof(saved_slots));
	memset(ac->srv_resolver, 0, sizeof(ac->srv_resolver));
	ac->resolver_size = 0;
	if (!ac->resolver)
		ac->resolver = alligator_ht_init(NULL);

	js = json_string("tcp://192.0.2.1:53");
	resolver_push_json(js);
	json_decref(js);

	hi = parse_url(url, strlen(url));
	client = hi ? context_arg_json_fill(NULL, hi, NULL, "blackbox", NULL, 0, NULL, NULL, 0, ac->loop, NULL, 0, NULL, 0) : NULL;
	url_free(hi);
	if (!client || !ac->resolver_size) {
		if (client)
			carg_free(client);
		goto restore;
	}

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		aggregator_get_addr(client, (char *)name, DNS_TYPE_A, DNS_CLASS_IN) == NULL);
	probe = resolver_ephemeral_lookup(name, DNS_TYPE_A);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, probe != NULL);
	if (!probe) {
		carg_free(client);
		goto restore;
	}

	uv_timer_stop(&probe->resolver_timer);
	memset(&probe->client, 0, sizeof(probe->client));
	probe->client.data = probe;
	uv_tcp_init(ac->loop, &probe->client);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, resolver_tcp_halt(probe));
	test_resolver_drain_closes();
	/* Close_cb must have zeroed the handle (same as UDP). */
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, resolver_tcp_halt(probe));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
		probe->client.loop && probe->client.type != UV_UNKNOWN_HANDLE);

	resolver_ephemeral_release(probe);
	/* Only the live resolver_timer should be outstanding — not a dead TCP. */
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, (int)resolver_ephemeral_count());
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, probe->resolver_ephemeral_closes);
	test_resolver_drain_closes();
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, (int)resolver_ephemeral_count());
	carg_free(client);

restore:
	if (ac->resolver_size) {
		rd = ac->srv_resolver[0];
		ac->srv_resolver[0] = NULL;
		ac->resolver_size = 0;
		if (rd) {
			url_free(rd->hi);
			labels_hash_free(rd->labels);
			free_percentile_buffer(rd->response_time);
			free_percentile_buffer(rd->read_time);
			free_percentile_buffer(rd->write_time);
			free(rd);
		}
	}
	memcpy(ac->srv_resolver, saved_slots, sizeof(saved_slots));
	ac->resolver_size = saved_size;
}

/* Shared-bind release must wait for an in-flight uv_udp_send before carg_free.
 * Simulate send_active without a peer: release sets wait_send, then send_cb path
 * (note_closed via wait_send) plus timer drain frees the probe. */
static void test_resolver_ephemeral_shared_send_waits(void)
{
	resolver_data *saved_slots[255];
	uint8_t saved_size;
	json_t *js;
	char url[] = "http://brand.example/";
	host_aggregator_info *hi;
	context_arg *client;
	context_arg *probe;
	const char *name = "ephemeral-shared-send.example";
	resolver_data *rd;

	if (!ac || !ac->loop)
		return;

	saved_size = ac->resolver_size;
	memcpy(saved_slots, ac->srv_resolver, sizeof(saved_slots));
	memset(ac->srv_resolver, 0, sizeof(ac->srv_resolver));
	ac->resolver_size = 0;
	if (!ac->resolver)
		ac->resolver = alligator_ht_init(NULL);

	js = json_string("udp://192.0.2.1:53");
	resolver_push_json(js);
	json_decref(js);

	hi = parse_url(url, strlen(url));
	client = hi ? context_arg_json_fill(NULL, hi, NULL, "blackbox", NULL, 0, NULL, NULL, 0, ac->loop, NULL, 0, NULL, 0) : NULL;
	url_free(hi);
	if (!client || !ac->resolver_size) {
		if (client)
			carg_free(client);
		goto restore;
	}

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		aggregator_get_addr(client, (char *)name, DNS_TYPE_A, DNS_CLASS_IN) == NULL);
	probe = resolver_ephemeral_lookup(name, DNS_TYPE_A);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, probe != NULL);
	if (!probe) {
		carg_free(client);
		goto restore;
	}

	/* Pretend a shared-bind send is still queued (no peer needed). */
	probe->resolver_udp_shared = 1;
	probe->resolver_udp_send_active = 1;
	resolver_ephemeral_release(probe);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, probe->resolver_ephemeral_freeing);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, probe->resolver_ephemeral_wait_send);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, probe->resolver_ephemeral_closes >= 1);
	/* Still tracked while wait_send/timer close are outstanding. */
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, (int)resolver_ephemeral_count());
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		resolver_ephemeral_lookup(name, DNS_TYPE_A) == probe);

	/* Same completion path as resolver_send_udp when wait_send is set. */
	probe->resolver_udp_send_active = 0;
	probe->resolver_ephemeral_wait_send = 0;
	resolver_ephemeral_note_closed(probe);
	test_resolver_drain_closes();
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, (int)resolver_ephemeral_count());
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		resolver_ephemeral_lookup(name, DNS_TYPE_A) == NULL);
	carg_free(client);

restore:
	if (ac->resolver_size) {
		rd = ac->srv_resolver[0];
		ac->srv_resolver[0] = NULL;
		ac->resolver_size = 0;
		if (rd) {
			url_free(rd->hi);
			labels_hash_free(rd->labels);
			free_percentile_buffer(rd->response_time);
			free_percentile_buffer(rd->read_time);
			free_percentile_buffer(rd->write_time);
			free(rd);
		}
	}
	memcpy(ac->srv_resolver, saved_slots, sizeof(saved_slots));
	ac->resolver_size = saved_size;
}

/* After probes_halt / stop, get_addr must not allocate a new ephemeral probe
 * (would miss release_all and be orphaned by ht_destroy). */
static void test_resolver_ephemeral_no_alloc_after_stop(void)
{
	resolver_data *saved_slots[255];
	uint8_t saved_size;
	json_t *js;
	char url[] = "http://brand.example/";
	host_aggregator_info *hi;
	context_arg *client;
	const char *name = "ephemeral-after-stop.example";
	resolver_data *rd;
	int was_stop;

	if (!ac || !ac->loop)
		return;

	saved_size = ac->resolver_size;
	memcpy(saved_slots, ac->srv_resolver, sizeof(saved_slots));
	memset(ac->srv_resolver, 0, sizeof(ac->srv_resolver));
	ac->resolver_size = 0;
	if (!ac->resolver)
		ac->resolver = alligator_ht_init(NULL);

	js = json_string("udp://192.0.2.1:53");
	resolver_push_json(js);
	json_decref(js);

	hi = parse_url(url, strlen(url));
	client = hi ? context_arg_json_fill(NULL, hi, NULL, "blackbox", NULL, 0, NULL, NULL, 0, ac->loop, NULL, 0, NULL, 0) : NULL;
	url_free(hi);
	if (!client || !ac->resolver_size) {
		if (client)
			carg_free(client);
		goto restore;
	}

	was_stop = alligator_stop_requested();
	alligator_stop_requested_set_for_test(1);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		aggregator_get_addr(client, (char *)name, DNS_TYPE_A, DNS_CLASS_IN) == NULL);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, (int)resolver_ephemeral_count());
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		resolver_ephemeral_lookup(name, DNS_TYPE_A) == NULL);
	alligator_stop_requested_set_for_test(was_stop);
	carg_free(client);

restore:
	alligator_stop_requested_set_for_test(0);
	if (ac->resolver_size) {
		rd = ac->srv_resolver[0];
		ac->srv_resolver[0] = NULL;
		ac->resolver_size = 0;
		if (rd) {
			url_free(rd->hi);
			labels_hash_free(rd->labels);
			free_percentile_buffer(rd->response_time);
			free_percentile_buffer(rd->read_time);
			free_percentile_buffer(rd->write_time);
			free(rd);
		}
	}
	memcpy(ac->srv_resolver, saved_slots, sizeof(saved_slots));
	ac->resolver_size = saved_size;
}

/* resolver_closed_tcp must free length-prefix write_buffer so release/close
 * cannot leak when writed_tcp does not run. */
static void test_resolver_ephemeral_tcp_write_buffer_freed_on_close(void)
{
	resolver_data *saved_slots[255];
	uint8_t saved_size;
	json_t *js;
	char url[] = "http://brand.example/";
	host_aggregator_info *hi;
	context_arg *client;
	context_arg *probe;
	const char *name = "ephemeral-write-buf.example";
	resolver_data *rd;
	char *owned;

	if (!ac || !ac->loop)
		return;

	saved_size = ac->resolver_size;
	memcpy(saved_slots, ac->srv_resolver, sizeof(saved_slots));
	memset(ac->srv_resolver, 0, sizeof(ac->srv_resolver));
	ac->resolver_size = 0;
	if (!ac->resolver)
		ac->resolver = alligator_ht_init(NULL);

	js = json_string("tcp://192.0.2.1:53");
	resolver_push_json(js);
	json_decref(js);

	hi = parse_url(url, strlen(url));
	client = hi ? context_arg_json_fill(NULL, hi, NULL, "blackbox", NULL, 0, NULL, NULL, 0, ac->loop, NULL, 0, NULL, 0) : NULL;
	url_free(hi);
	if (!client || !ac->resolver_size) {
		if (client)
			carg_free(client);
		goto restore;
	}

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		aggregator_get_addr(client, (char *)name, DNS_TYPE_A, DNS_CLASS_IN) == NULL);
	probe = resolver_ephemeral_lookup(name, DNS_TYPE_A);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, probe != NULL);
	if (!probe) {
		carg_free(client);
		goto restore;
	}

	uv_timer_stop(&probe->resolver_timer);
	memset(&probe->client, 0, sizeof(probe->client));
	probe->client.data = probe;
	uv_tcp_init(ac->loop, &probe->client);
	owned = malloc(2);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, owned != NULL);
	if (!owned) {
		resolver_ephemeral_release(probe);
		test_resolver_drain_closes();
		carg_free(client);
		goto restore;
	}
	owned[0] = 0;
	owned[1] = 42;
	probe->write_buffer = uv_buf_init(owned, 2);

	resolver_ephemeral_release(probe);
	test_resolver_drain_closes();
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, (int)resolver_ephemeral_count());
	carg_free(client);

restore:
	if (ac->resolver_size) {
		rd = ac->srv_resolver[0];
		ac->srv_resolver[0] = NULL;
		ac->resolver_size = 0;
		if (rd) {
			url_free(rd->hi);
			labels_hash_free(rd->labels);
			free_percentile_buffer(rd->response_time);
			free_percentile_buffer(rd->read_time);
			free_percentile_buffer(rd->write_time);
			free(rd);
		}
	}
	memcpy(ac->srv_resolver, saved_slots, sizeof(saved_slots));
	ac->resolver_size = saved_size;
}

/* Signal-path probes_halt must not free while uv_udp_send is still owned by
 * libuv (UAF). Post-loop resolver_ephemeral_shutdown finishes wait_send only
 * after send is no longer active (ECANCELED / send_cb). */
static void test_resolver_ephemeral_halt_drains_wait_send(void)
{
	resolver_data *saved_slots[255];
	uint8_t saved_size;
	json_t *js;
	char url[] = "http://brand.example/";
	host_aggregator_info *hi;
	context_arg *client;
	context_arg *probe;
	const char *name = "ephemeral-halt-wait.example";
	resolver_data *rd;

	if (!ac || !ac->loop)
		return;

	saved_size = ac->resolver_size;
	memcpy(saved_slots, ac->srv_resolver, sizeof(saved_slots));
	memset(ac->srv_resolver, 0, sizeof(ac->srv_resolver));
	ac->resolver_size = 0;
	if (!ac->resolver)
		ac->resolver = alligator_ht_init(NULL);

	js = json_string("udp://192.0.2.1:53");
	resolver_push_json(js);
	json_decref(js);

	hi = parse_url(url, strlen(url));
	client = hi ? context_arg_json_fill(NULL, hi, NULL, "blackbox", NULL, 0, NULL, NULL, 0, ac->loop, NULL, 0, NULL, 0) : NULL;
	url_free(hi);
	if (!client || !ac->resolver_size) {
		if (client)
			carg_free(client);
		goto restore;
	}

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		aggregator_get_addr(client, (char *)name, DNS_TYPE_A, DNS_CLASS_IN) == NULL);
	probe = resolver_ephemeral_lookup(name, DNS_TYPE_A);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, probe != NULL);
	if (!probe) {
		carg_free(client);
		goto restore;
	}

	probe->resolver_udp_shared = 1;
	probe->resolver_udp_send_active = 1;
	resolver_ephemeral_release(probe);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, probe->resolver_ephemeral_wait_send);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, (int)resolver_ephemeral_count());

	/* Signal-equivalent path: arm only — must not free while send_active. */
	resolver_probes_halt();
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, (int)resolver_ephemeral_count());
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, probe->resolver_ephemeral_wait_send);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, probe->resolver_udp_send_active);

	/* Post-loop path must not force-free while send still active (UAF). */
	resolver_ephemeral_shutdown();
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, (int)resolver_ephemeral_count());
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, probe->resolver_ephemeral_wait_send);

	/* Simulate ECANCELED / send_cb: clear active, then shutdown finishes wait. */
	probe->resolver_udp_send_active = 0;
	resolver_ephemeral_shutdown();
	test_resolver_drain_closes();
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, (int)resolver_ephemeral_count());
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		resolver_ephemeral_lookup(name, DNS_TYPE_A) == NULL);

	/* Stuck wait_send with no in-flight send: post-loop cancel must drain. */
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		aggregator_get_addr(client, (char *)name, DNS_TYPE_A, DNS_CLASS_IN) == NULL);
	probe = resolver_ephemeral_lookup(name, DNS_TYPE_A);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, probe != NULL);
	if (probe) {
		probe->resolver_udp_shared = 1;
		probe->resolver_udp_send_active = 0;
		resolver_ephemeral_release(probe);
		/* Manually arm wait_send without send_active (stuck state). */
		if (probe->resolver_ephemeral_freeing && !probe->resolver_ephemeral_wait_send) {
			probe->resolver_ephemeral_wait_send = 1;
			probe->resolver_ephemeral_closes++;
		}
		resolver_probes_halt();
		assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, (int)resolver_ephemeral_count());
		resolver_ephemeral_shutdown();
		test_resolver_drain_closes();
		assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, (int)resolver_ephemeral_count());
	}
	carg_free(client);

restore:
	if (ac->resolver_size) {
		rd = ac->srv_resolver[0];
		ac->srv_resolver[0] = NULL;
		ac->resolver_size = 0;
		if (rd) {
			url_free(rd->hi);
			labels_hash_free(rd->labels);
			free_percentile_buffer(rd->response_time);
			free_percentile_buffer(rd->read_time);
			free_percentile_buffer(rd->write_time);
			free(rd);
		}
	}
	memcpy(ac->srv_resolver, saved_slots, sizeof(saved_slots));
	ac->resolver_size = saved_size;
}

static void test_dns_resolution_failure_metrics(void)
{
	resolver_data *saved_slots[255];
	resolver_data *rd;
	uint8_t saved_size;
	json_t *js;
	char url[] = "http://fail.example/";
	host_aggregator_info *hi;
	context_arg *client;
	context_arg *probe;
	const char *name = "unreachable.example";

	if (!ac || !ac->loop)
		return;

	saved_size = ac->resolver_size;
	memcpy(saved_slots, ac->srv_resolver, sizeof(saved_slots));
	memset(ac->srv_resolver, 0, sizeof(ac->srv_resolver));
	ac->resolver_size = 0;
	if (!ac->resolver)
		ac->resolver = alligator_ht_init(NULL);

	js = json_string("udp://192.0.2.1:53");
	resolver_push_json(js);
	json_decref(js);

	hi = parse_url(url, strlen(url));
	client = hi ? context_arg_json_fill(NULL, hi, NULL, "blackbox", NULL, 0, NULL, NULL, 0, ac->loop, NULL, 0, NULL, 0) : NULL;
	url_free(hi);
	if (!client || !ac->resolver_size) {
		if (client)
			carg_free(client);
		goto restore;
	}

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		aggregator_get_addr(client, (char *)name, DNS_TYPE_A, DNS_CLASS_IN) == NULL);
	probe = resolver_ephemeral_lookup(name, DNS_TYPE_A);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, probe != NULL);
	if (probe) {
		assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, probe->dns_status_reported);
		/* Transport failure never reaches dns_handler: the fail path must
		 * still leave a 0 gauge and an error counter for the name. */
		resolver_ephemeral_fail(probe);
		assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, probe->dns_status_reported);
		metric_test_run(CMP_EQUAL,
			"alligator_dns_resolution_status{name=\"unreachable.example\",type=\"A\",class=\"IN\"}",
			"alligator_dns_resolution_status", 0);
		metric_test_run(CMP_EQUAL,
			"alligator_dns_resolutions_total{name=\"unreachable.example\",type=\"A\",class=\"IN\",result=\"error\"}",
			"alligator_dns_resolutions_total", 1);
		test_resolver_drain_closes();
		assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, (int)resolver_ephemeral_count());
	}
	carg_free(client);

restore:
	if (ac->resolver_size) {
		rd = ac->srv_resolver[0];
		ac->srv_resolver[0] = NULL;
		ac->resolver_size = 0;
		if (rd) {
			url_free(rd->hi);
			labels_hash_free(rd->labels);
			free_percentile_buffer(rd->response_time);
			free_percentile_buffer(rd->read_time);
			free_percentile_buffer(rd->write_time);
			free(rd);
		}
	}
	memcpy(ac->srv_resolver, saved_slots, sizeof(saved_slots));
	ac->resolver_size = saved_size;
}

void test_resolver_dns_pack_unpack(void)
{
	test_dns_name_encode_decode();
	test_dns_name_decode_compression();
	test_dns_query_pack_unpack();
	test_dns_init_type_and_strtype();
	test_dns_response_pack_roundtrip();
	test_resolver_udp_match_pending_txid_and_qname();
	test_alloc_buffer_ignores_non_carg_handle_data();
	test_dns_handler_parse_response();
	test_dns_unpack_invalid();
	test_vrl_dns_reverse_name();
	test_resolver_cache_lookup();
	test_resolver_ephemeral_retry_frees();
	test_resolver_ephemeral_tcp_write_fail_unsticks();
	test_resolver_tcp_permanent_fail_closes();
	test_resolver_tcp_permanent_success_halts();
	test_resolver_ephemeral_connect_tcp_reentry_releases();
	test_resolver_ephemeral_connect_udp_reentry_releases();
	test_resolver_ephemeral_release_while_closing();
	test_resolver_ephemeral_release_after_tcp_closed();
	test_resolver_ephemeral_shared_send_waits();
	test_resolver_ephemeral_halt_drains_wait_send();
	test_resolver_ephemeral_no_alloc_after_stop();
	test_resolver_ephemeral_tcp_write_buffer_freed_on_close();
	test_dns_resolution_failure_metrics();
}
