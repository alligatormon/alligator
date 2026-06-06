#include <string.h>
#include <arpa/inet.h>
#include "main.h"
#include "resolver/dns.h"
#include "resolver/resolver.h"
#include "events/context_arg.h"

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

	carg.resolver = 1;
	carg.parsed = 0;
	carg.curr_ttl = -1;
	ttl = dns_handler((char*)pkt, sizeof(pkt), &carg);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, carg.parsed);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 30, (int)carg.curr_ttl);
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
}

void test_resolver_dns_pack_unpack(void)
{
	test_dns_name_encode_decode();
	test_dns_name_decode_compression();
	test_dns_query_pack_unpack();
	test_dns_init_type_and_strtype();
	test_dns_response_pack_roundtrip();
	test_dns_handler_parse_response();
	test_dns_unpack_invalid();
}
