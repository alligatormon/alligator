#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "metric/labels.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/selector.h"
#include "common/url.h"
#include "common/logs.h"
#include "main.h"

extern int env_struct_compare(const void *arg, const void *obj);

/* Minimal LDAP (RFC 4511) client for OpenLDAP cn=Monitor scrapes.
 * Pipelines BindRequest + SearchRequest; TLS via tls:// (LDAPS). */

#define LDAP_MSG_BIND 1
#define LDAP_MSG_SEARCH 2

#define LDAP_TAG_SEQ 0x30
#define LDAP_TAG_INT 0x02
#define LDAP_TAG_OCTET 0x04
#define LDAP_TAG_ENUM 0x0A
#define LDAP_TAG_BOOL 0x01
#define LDAP_TAG_SET 0x31

#define LDAP_APP_BIND_REQ 0x60
#define LDAP_APP_BIND_RES 0x61
#define LDAP_APP_SEARCH_REQ 0x63
#define LDAP_APP_SEARCH_ENTRY 0x64
#define LDAP_APP_SEARCH_DONE 0x65

#define LDAP_CTX_SIMPLE 0x80
#define LDAP_FILTER_OR 0xA1
#define LDAP_FILTER_EQ 0xA3

#define LDAP_SCOPE_SUBTREE 2
#define LDAP_DEREF_NEVER 0

#define LDAP_RC_SUCCESS 0
#define LDAP_RC_TIME_LIMIT 3
#define LDAP_RC_SIZE_LIMIT 4

/* Monitor entries are small; a larger claimed PDU usually means BER desync. */
#define LDAP_MAX_PDU 65536
#define LDAP_SEARCH_SIZE_LIMIT 500
#define LDAP_SEARCH_TIME_LIMIT 5

#define LDAP_DN_MAX 512
#define LDAP_ATTR_MAX 128
#define LDAP_VAL_MAX 256

typedef struct {
	unsigned char *buf;
	size_t len;
	size_t cap;
} ldap_buf;

static int ldap_buf_reserve(ldap_buf *b, size_t need)
{
	unsigned char *n;
	size_t ncap;

	if (b->len + need <= b->cap)
		return 0;
	ncap = b->cap ? b->cap : 64;
	while (ncap < b->len + need)
		ncap *= 2;
	n = realloc(b->buf, ncap);
	if (!n)
		return -1;
	b->buf = n;
	b->cap = ncap;
	return 0;
}

static int ldap_buf_append(ldap_buf *b, const void *p, size_t n)
{
	if (ldap_buf_reserve(b, n))
		return -1;
	memcpy(b->buf + b->len, p, n);
	b->len += n;
	return 0;
}

static int ldap_buf_u8(ldap_buf *b, unsigned char v)
{
	return ldap_buf_append(b, &v, 1);
}

static int ldap_tlv_start(ldap_buf *b, unsigned char tag, size_t *mark)
{
	if (ldap_buf_u8(b, tag))
		return -1;
	*mark = b->len;
	/* placeholder short length; rewritten by ldap_tlv_end */
	if (ldap_buf_u8(b, 0))
		return -1;
	return 0;
}

static int ldap_tlv_end(ldap_buf *b, size_t mark)
{
	size_t content = b->len - mark - 1;
	unsigned char enc[5];
	size_t enc_len;
	size_t shift;
	size_t i;

	if (content < 0x80) {
		b->buf[mark] = (unsigned char)content;
		return 0;
	}
	if (content <= 0xFF) {
		enc[0] = 0x81;
		enc[1] = (unsigned char)content;
		enc_len = 2;
	} else if (content <= 0xFFFF) {
		enc[0] = 0x82;
		enc[1] = (unsigned char)(content >> 8);
		enc[2] = (unsigned char)content;
		enc_len = 3;
	} else {
		enc[0] = 0x84;
		enc[1] = (unsigned char)(content >> 24);
		enc[2] = (unsigned char)(content >> 16);
		enc[3] = (unsigned char)(content >> 8);
		enc[4] = (unsigned char)content;
		enc_len = 5;
	}
	shift = enc_len - 1;
	if (ldap_buf_reserve(b, shift))
		return -1;
	memmove(b->buf + mark + enc_len, b->buf + mark + 1, content);
	for (i = 0; i < enc_len; ++i)
		b->buf[mark + i] = enc[i];
	b->len += shift;
	return 0;
}

static int ldap_put_int(ldap_buf *b, int64_t v)
{
	unsigned char raw[8];
	size_t n = 0;
	uint64_t u;
	size_t mark;

	/* Message IDs and LDAP integers used here are non-negative. */
	if (v < 0)
		v = 0;
	u = (uint64_t)v;

	if (u == 0) {
		raw[0] = 0;
		n = 1;
	} else {
		while (u) {
			raw[7 - n] = (unsigned char)(u & 0xFF);
			u >>= 8;
			++n;
		}
		memmove(raw, raw + 8 - n, n);
		if (raw[0] & 0x80) {
			memmove(raw + 1, raw, n);
			raw[0] = 0;
			++n;
		}
	}

	if (ldap_tlv_start(b, LDAP_TAG_INT, &mark))
		return -1;
	if (ldap_buf_append(b, raw, n))
		return -1;
	return ldap_tlv_end(b, mark);
}

static int ldap_put_enum(ldap_buf *b, int v)
{
	size_t mark;
	if (ldap_tlv_start(b, LDAP_TAG_ENUM, &mark))
		return -1;
	if (ldap_buf_u8(b, (unsigned char)v))
		return -1;
	return ldap_tlv_end(b, mark);
}

static int ldap_put_bool(ldap_buf *b, int v)
{
	size_t mark;
	if (ldap_tlv_start(b, LDAP_TAG_BOOL, &mark))
		return -1;
	if (ldap_buf_u8(b, v ? 0xFF : 0x00))
		return -1;
	return ldap_tlv_end(b, mark);
}

static int ldap_put_octet(ldap_buf *b, const char *s, size_t len)
{
	size_t mark;
	if (ldap_tlv_start(b, LDAP_TAG_OCTET, &mark))
		return -1;
	if (len && ldap_buf_append(b, s, len))
		return -1;
	return ldap_tlv_end(b, mark);
}

static int ldap_put_octet_str(ldap_buf *b, const char *s)
{
	return ldap_put_octet(b, s, s ? strlen(s) : 0);
}

static int ldap_put_simple_auth(ldap_buf *b, const char *pass)
{
	size_t mark;
	size_t len = pass ? strlen(pass) : 0;
	if (ldap_tlv_start(b, LDAP_CTX_SIMPLE, &mark))
		return -1;
	if (len && ldap_buf_append(b, pass, len))
		return -1;
	return ldap_tlv_end(b, mark);
}

static int ldap_put_eq_filter(ldap_buf *b, const char *attr, const char *val)
{
	size_t mark;
	if (ldap_tlv_start(b, LDAP_FILTER_EQ, &mark))
		return -1;
	if (ldap_put_octet_str(b, attr))
		return -1;
	if (ldap_put_octet_str(b, val))
		return -1;
	return ldap_tlv_end(b, mark);
}

static int ldap_put_monitor_filter(ldap_buf *b)
{
	size_t mark;
	if (ldap_tlv_start(b, LDAP_FILTER_OR, &mark))
		return -1;
	if (ldap_put_eq_filter(b, "objectClass", "monitorCounterObject"))
		return -1;
	if (ldap_put_eq_filter(b, "objectClass", "monitoredObject"))
		return -1;
	if (ldap_put_eq_filter(b, "objectClass", "monitorOperation"))
		return -1;
	return ldap_tlv_end(b, mark);
}

static int ldap_put_attrlist(ldap_buf *b)
{
	size_t mark;
	if (ldap_tlv_start(b, LDAP_TAG_SEQ, &mark))
		return -1;
	/* Only metric-bearing attrs — skip objectClass to keep PDUs small. */
	if (ldap_put_octet_str(b, "monitorCounter"))
		return -1;
	if (ldap_put_octet_str(b, "monitoredInfo"))
		return -1;
	if (ldap_put_octet_str(b, "monitorOpCompleted"))
		return -1;
	return ldap_tlv_end(b, mark);
}

static int ldap_wrap_message(ldap_buf *out, int msgid, const ldap_buf *op)
{
	size_t mark;
	if (ldap_tlv_start(out, LDAP_TAG_SEQ, &mark))
		return -1;
	if (ldap_put_int(out, msgid))
		return -1;
	if (ldap_buf_append(out, op->buf, op->len))
		return -1;
	return ldap_tlv_end(out, mark);
}

static int ldap_build_bind(ldap_buf *out, const char *dn, const char *pass)
{
	ldap_buf op = {0};
	size_t mark;
	int rc = -1;

	if (ldap_tlv_start(&op, LDAP_APP_BIND_REQ, &mark))
		goto done;
	if (ldap_put_int(&op, 3))
		goto done;
	if (ldap_put_octet_str(&op, dn ? dn : ""))
		goto done;
	if (ldap_put_simple_auth(&op, pass ? pass : ""))
		goto done;
	if (ldap_tlv_end(&op, mark))
		goto done;
	rc = ldap_wrap_message(out, LDAP_MSG_BIND, &op);
done:
	free(op.buf);
	return rc;
}

static int ldap_build_search(ldap_buf *out, const char *base)
{
	ldap_buf op = {0};
	size_t mark;
	int rc = -1;

	if (ldap_tlv_start(&op, LDAP_APP_SEARCH_REQ, &mark))
		goto done;
	if (ldap_put_octet_str(&op, base ? base : "cn=Monitor"))
		goto done;
	if (ldap_put_enum(&op, LDAP_SCOPE_SUBTREE))
		goto done;
	if (ldap_put_enum(&op, LDAP_DEREF_NEVER))
		goto done;
	if (ldap_put_int(&op, LDAP_SEARCH_SIZE_LIMIT))
		goto done;
	if (ldap_put_int(&op, LDAP_SEARCH_TIME_LIMIT))
		goto done;
	if (ldap_put_bool(&op, 0))
		goto done;
	if (ldap_put_monitor_filter(&op))
		goto done;
	if (ldap_put_attrlist(&op))
		goto done;
	if (ldap_tlv_end(&op, mark))
		goto done;
	rc = ldap_wrap_message(out, LDAP_MSG_SEARCH, &op);
done:
	free(op.buf);
	return rc;
}

/* 0 = ok, 1 = need more bytes, -1 = framing error (indefinite length, absurd). */
static int ldap_ber_read_len(const unsigned char *p, size_t avail, size_t *len_out, size_t *hdr_out)
{
	size_t n;
	size_t i;
	size_t L = 0;

	if (avail < 2)
		return 1;
	if (p[1] < 0x80) {
		*len_out = p[1];
		*hdr_out = 2;
		return 0;
	}
	n = p[1] & 0x7F;
	if (!n || n > 4)
		return -1;
	if (avail < 2 + n)
		return 1;
	for (i = 0; i < n; ++i)
		L = (L << 8) | p[2 + i];
	*len_out = L;
	*hdr_out = 2 + n;
	return 0;
}

/* Returns total LDAPMessage size including tag+len, or 0 if incomplete, -1 on error. */
static ssize_t ldap_message_size(const unsigned char *p, size_t avail)
{
	size_t len;
	size_t hdr;
	int rc;

	if (!avail)
		return 0;
	if (p[0] != LDAP_TAG_SEQ)
		return -1;
	rc = ldap_ber_read_len(p, avail, &len, &hdr);
	if (rc > 0)
		return 0;
	if (rc < 0)
		return -1;
	if (len > LDAP_MAX_PDU)
		return -1;
	if (avail < hdr + len)
		return 0;
	return (ssize_t)(hdr + len);
}

static int ldap_parse_int(const unsigned char *p, size_t len, int64_t *out)
{
	size_t i;
	int64_t v = 0;

	if (!len || len > 8)
		return -1;
	if (p[0] & 0x80) {
		v = -1;
		for (i = 0; i < len; ++i)
			v = (v << 8) | p[i];
	} else {
		for (i = 0; i < len; ++i)
			v = (v << 8) | p[i];
	}
	*out = v;
	return 0;
}

static int ldap_enter_tlv(const unsigned char **pp, size_t *left, unsigned char expect_tag,
	const unsigned char **content, size_t *content_len)
{
	size_t len;
	size_t hdr;

	if (*left < 1)
		return -1;
	if (expect_tag && (*pp)[0] != expect_tag)
		return -1;
	if (ldap_ber_read_len(*pp, *left, &len, &hdr) != 0)
		return -1;
	if (*left < hdr + len)
		return -1;
	*content = *pp + hdr;
	*content_len = len;
	*pp += hdr + len;
	*left -= hdr + len;
	return 0;
}

static void ldap_help(context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "openldap_up", METRIC_TYPE_GAUGE,
		"1 if the OpenLDAP monitor scrape succeeded.");
	namespace_metric_family_set(NULL, carg, "openldap_bind_result_code", METRIC_TYPE_GAUGE,
		"LDAP resultCode from BindResponse (-1 if missing).");
	namespace_metric_family_set(NULL, carg, "openldap_search_result_code", METRIC_TYPE_GAUGE,
		"LDAP resultCode from SearchResultDone (-1 if missing).");
	namespace_metric_family_set(NULL, carg, "openldap_monitor_counter_object", METRIC_TYPE_GAUGE,
		"OpenLDAP monitorCounter under cn=Monitor.");
	namespace_metric_family_set(NULL, carg, "openldap_monitored_object", METRIC_TYPE_GAUGE,
		"OpenLDAP monitoredInfo under cn=Monitor.");
	namespace_metric_family_set(NULL, carg, "openldap_monitor_operation", METRIC_TYPE_GAUGE,
		"OpenLDAP monitorOpCompleted under cn=Operations,cn=Monitor.");
}

static int ldap_attr_is_numeric(const char *s, size_t n, double *out)
{
	char tmp[LDAP_VAL_MAX];
	char *end = NULL;
	size_t i;
	size_t c;

	if (!n || n >= sizeof(tmp))
		return 0;
	for (i = 0; i < n; ++i) {
		if (!isdigit((unsigned char)s[i]) && s[i] != '.' && s[i] != '-' && s[i] != '+')
			return 0;
	}
	c = n < sizeof(tmp) - 1 ? n : sizeof(tmp) - 1;
	memcpy(tmp, s, c);
	tmp[c] = 0;
	*out = strtod(tmp, &end);
	return end && end != tmp && *end == 0;
}

static void ldap_emit(context_arg *carg, const char *metric, const char *dn, double val)
{
	alligator_ht *lbl;

	if (!metric || !dn || !dn[0])
		return;
	lbl = alligator_ht_init(NULL);
	labels_hash_insert_nocache(lbl, "dn", (char *)dn);
	metric_add((char *)metric, lbl, &val, DATATYPE_DOUBLE, carg);
}

static void ldap_parse_partial_attr(const unsigned char *p, size_t len,
	char *atype, size_t atype_sz, char *aval, size_t aval_sz, size_t *aval_len)
{
	const unsigned char *cur = p;
	size_t left = len;
	const unsigned char *c;
	size_t clen;
	const unsigned char *setc;
	size_t setlen;
	const unsigned char *vc;
	size_t vlen;

	atype[0] = 0;
	aval[0] = 0;
	*aval_len = 0;

	if (ldap_enter_tlv(&cur, &left, LDAP_TAG_OCTET, &c, &clen))
		return;
	if (clen >= atype_sz)
		clen = atype_sz - 1;
	memcpy(atype, c, clen);
	atype[clen] = 0;

	if (left < 1)
		return;
	/* SET OF values */
	if (cur[0] != LDAP_TAG_SET)
		return;
	if (ldap_enter_tlv(&cur, &left, LDAP_TAG_SET, &setc, &setlen))
		return;
	cur = setc;
	left = setlen;
	if (!left)
		return;
	if (ldap_enter_tlv(&cur, &left, LDAP_TAG_OCTET, &vc, &vlen))
		return;
	if (vlen >= aval_sz)
		vlen = aval_sz - 1;
	memcpy(aval, vc, vlen);
	aval[vlen] = 0;
	*aval_len = vlen;
}

static int ldap_parse_search_entry(context_arg *carg, const unsigned char *p, size_t len)
{
	const unsigned char *cur = p;
	size_t left = len;
	const unsigned char *c;
	size_t clen;
	char dn[LDAP_DN_MAX];
	char atype[LDAP_ATTR_MAX];
	char aval[LDAP_VAL_MAX];
	size_t aval_len;
	double dval;
	int emitted = 0;
	char counter_val[LDAP_VAL_MAX];
	char info_val[LDAP_VAL_MAX];
	char op_val[LDAP_VAL_MAX];
	int have_counter = 0, have_info = 0, have_op = 0;

	counter_val[0] = info_val[0] = op_val[0] = 0;

	if (ldap_enter_tlv(&cur, &left, LDAP_TAG_OCTET, &c, &clen))
		return 0;
	if (clen >= sizeof(dn))
		clen = sizeof(dn) - 1;
	memcpy(dn, c, clen);
	dn[clen] = 0;

	if (left < 1 || cur[0] != LDAP_TAG_SEQ)
		return 0;
	if (ldap_enter_tlv(&cur, &left, LDAP_TAG_SEQ, &c, &clen))
		return 0;

	cur = c;
	left = clen;
	while (left > 0) {
		const unsigned char *attr;
		size_t attrlen;
		if (cur[0] != LDAP_TAG_SEQ)
			break;
		if (ldap_enter_tlv(&cur, &left, LDAP_TAG_SEQ, &attr, &attrlen))
			break;
		ldap_parse_partial_attr(attr, attrlen, atype, sizeof(atype), aval, sizeof(aval), &aval_len);
		if (!atype[0] || !aval_len)
			continue;
		if (!strcasecmp(atype, "monitorCounter")) {
			memcpy(counter_val, aval, aval_len + 1);
			have_counter = 1;
		} else if (!strcasecmp(atype, "monitoredInfo")) {
			memcpy(info_val, aval, aval_len + 1);
			have_info = 1;
		} else if (!strcasecmp(atype, "monitorOpCompleted")) {
			memcpy(op_val, aval, aval_len + 1);
			have_op = 1;
		}
	}

	if (have_counter && ldap_attr_is_numeric(counter_val, strlen(counter_val), &dval)) {
		ldap_emit(carg, "openldap_monitor_counter_object", dn, dval);
		++emitted;
	}
	if (have_info && ldap_attr_is_numeric(info_val, strlen(info_val), &dval)) {
		ldap_emit(carg, "openldap_monitored_object", dn, dval);
		++emitted;
	}
	if (have_op && ldap_attr_is_numeric(op_val, strlen(op_val), &dval)) {
		ldap_emit(carg, "openldap_monitor_operation", dn, dval);
		++emitted;
	}
	return emitted;
}

static int ldap_parse_result_code(const unsigned char *p, size_t len, int64_t *code)
{
	const unsigned char *cur = p;
	size_t left = len;
	const unsigned char *c;
	size_t clen;

	/* LDAPResult: ENUMERATED resultCode, ... */
	if (left < 1 || cur[0] != LDAP_TAG_ENUM)
		return -1;
	if (ldap_enter_tlv(&cur, &left, LDAP_TAG_ENUM, &c, &clen))
		return -1;
	return ldap_parse_int(c, clen, code);
}

/* LDAPResult → resultCode + diagnosticMessage (best-effort). */
static int ldap_parse_ldap_result(const unsigned char *p, size_t len, int64_t *code,
	char *diag, size_t diag_sz)
{
	const unsigned char *cur = p;
	size_t left = len;
	const unsigned char *c;
	size_t clen;

	if (diag && diag_sz)
		diag[0] = 0;
	if (ldap_parse_result_code(cur, left, code))
		return -1;
	/* Skip past resultCode TLV, then matchedDN, then diagnosticMessage. */
	if (ldap_enter_tlv(&cur, &left, LDAP_TAG_ENUM, &c, &clen))
		return -1;
	if (left < 1 || cur[0] != LDAP_TAG_OCTET)
		return 0;
	if (ldap_enter_tlv(&cur, &left, LDAP_TAG_OCTET, &c, &clen))
		return 0;
	if (left < 1 || cur[0] != LDAP_TAG_OCTET)
		return 0;
	if (ldap_enter_tlv(&cur, &left, LDAP_TAG_OCTET, &c, &clen))
		return 0;
	if (diag && diag_sz) {
		size_t n = clen < diag_sz - 1 ? clen : diag_sz - 1;
		memcpy(diag, c, n);
		diag[n] = 0;
	}
	return 0;
}

static const char *ldap_result_name(int64_t rc)
{
	switch (rc) {
	case 0: return "success";
	case 1: return "operationsError";
	case 2: return "protocolError";
	case 3: return "timeLimitExceeded";
	case 4: return "sizeLimitExceeded";
	case 32: return "noSuchObject";
	case 48: return "inappropriateAuthentication";
	case 49: return "invalidCredentials";
	case 50: return "insufficientAccessRights";
	case 53: return "unwillingToPerform";
	default: return "other";
	}
}

int8_t ldap_validator(context_arg *carg, char *data, size_t size)
{
	const unsigned char *p = (const unsigned char *)data;
	size_t off = 0;
	int saw_search_done = 0;
	int bind_failed = 0;
	int nmsg = 0;
	int nentry = 0;

	while (off < size) {
		ssize_t msz = ldap_message_size(p + off, size - off);
		const unsigned char *msg;
		size_t msg_left;
		const unsigned char *content;
		size_t content_len;
		const unsigned char *c;
		size_t clen;
		int64_t msgid;
		unsigned char op;

		if (msz == 0) {
			if (saw_search_done || bind_failed)
				return 1;
			carglog(carg, L_DEBUG,
				"ldap validator: incomplete PDU off=%zu size=%zu msgs=%d entries=%d head=%02x\n",
				off, size, nmsg, nentry, off < size ? p[off] : 0);
			return 0;
		}
		if (msz < 0) {
			carglog(carg, L_ERROR,
				"ldap validator: framing error off=%zu size=%zu msgs=%d (completing)\n",
				off, size, nmsg);
			return 1;
		}

		msg = p + off;
		msg_left = (size_t)msz;
		if (ldap_enter_tlv(&msg, &msg_left, LDAP_TAG_SEQ, &content, &content_len))
			return 1;
		{
			const unsigned char *cur = content;
			size_t left = content_len;
			if (left < 1 || cur[0] != LDAP_TAG_INT)
				return 1;
			if (ldap_enter_tlv(&cur, &left, LDAP_TAG_INT, &c, &clen))
				return 1;
			if (ldap_parse_int(c, clen, &msgid))
				return 1;
			if (!left)
				return 1;
			op = cur[0];
			++nmsg;
			if (op == LDAP_APP_BIND_RES) {
				int64_t rc = -1;
				if (!ldap_enter_tlv(&cur, &left, LDAP_APP_BIND_RES, &c, &clen) &&
					!ldap_parse_result_code(c, clen, &rc) && rc != LDAP_RC_SUCCESS)
					bind_failed = 1;
			} else if (op == LDAP_APP_SEARCH_ENTRY || op == 0x73) {
				/* SearchResultEntry or SearchResultReference */
				++nentry;
			} else if (op == LDAP_APP_SEARCH_DONE) {
				saw_search_done = 1;
			}
		}
		off += (size_t)msz;
	}

	if (saw_search_done || bind_failed) {
		carglog(carg, L_DEBUG,
			"ldap validator: complete done=%d bind_fail=%d msgs=%d entries=%d size=%zu\n",
			saw_search_done, bind_failed, nmsg, nentry, size);
		return 1;
	}
	carglog(carg, L_DEBUG,
		"ldap validator: waiting for SearchResultDone msgs=%d entries=%d size=%zu\n",
		nmsg, nentry, size);
	return 0;
}

void ldap_handler(char *metrics, size_t size, context_arg *carg)
{
	const unsigned char *p = (const unsigned char *)metrics;
	size_t off = 0;
	int entries = 0;
	int bind_ok = 0;
	int search_ok = 0;
	int64_t bind_rc = -1;
	int64_t search_rc = -1;
	char bind_diag[128];
	char search_diag[128];
	double up;
	double dbind;
	double dsearch;

	bind_diag[0] = search_diag[0] = 0;

	if (!metrics || !size) {
		carg->parser_status = 0;
		return;
	}

	ldap_help(carg);

	while (off < size) {
		ssize_t msz = ldap_message_size(p + off, size - off);
		const unsigned char *msg;
		size_t msg_left;
		const unsigned char *content;
		size_t content_len;
		const unsigned char *c;
		size_t clen;
		const unsigned char *cur;
		size_t left;
		int64_t msgid;
		unsigned char op;

		if (msz <= 0)
			break;

		msg = p + off;
		msg_left = (size_t)msz;
		if (ldap_enter_tlv(&msg, &msg_left, LDAP_TAG_SEQ, &content, &content_len))
			break;
		cur = content;
		left = content_len;
		if (left < 1 || cur[0] != LDAP_TAG_INT)
			break;
		if (ldap_enter_tlv(&cur, &left, LDAP_TAG_INT, &c, &clen))
			break;
		if (ldap_parse_int(c, clen, &msgid))
			break;
		if (!left)
			break;
		op = cur[0];
		if (op == LDAP_APP_BIND_RES) {
			if (!ldap_enter_tlv(&cur, &left, LDAP_APP_BIND_RES, &c, &clen) &&
				!ldap_parse_ldap_result(c, clen, &bind_rc, bind_diag, sizeof(bind_diag)) &&
				bind_rc == LDAP_RC_SUCCESS)
				bind_ok = 1;
		} else if (op == LDAP_APP_SEARCH_ENTRY) {
			if (!ldap_enter_tlv(&cur, &left, LDAP_APP_SEARCH_ENTRY, &c, &clen))
				entries += ldap_parse_search_entry(carg, c, clen);
		} else if (op == LDAP_APP_SEARCH_DONE) {
			if (!ldap_enter_tlv(&cur, &left, LDAP_APP_SEARCH_DONE, &c, &clen) &&
				!ldap_parse_ldap_result(c, clen, &search_rc, search_diag, sizeof(search_diag))) {
				/* size/time limit still return entries; treat as scrape OK. */
				if (search_rc == LDAP_RC_SUCCESS ||
					search_rc == LDAP_RC_SIZE_LIMIT ||
					search_rc == LDAP_RC_TIME_LIMIT)
					search_ok = 1;
			}
		}
		off += (size_t)msz;
	}

	dbind = (double)bind_rc;
	dsearch = (double)search_rc;
	metric_add_auto("openldap_bind_result_code", &dbind, DATATYPE_DOUBLE, carg);
	metric_add_auto("openldap_search_result_code", &dsearch, DATATYPE_DOUBLE, carg);

	up = (bind_ok && search_ok) ? 1.0 : 0.0;
	metric_add_auto("openldap_up", &up, DATATYPE_DOUBLE, carg);
	carg->parser_status = (bind_ok && search_ok) ? 1 : 0;

	if (!bind_ok)
		carglog(carg, L_ERROR,
			"ldap: BindResponse resultCode=%"PRId64" (%s) diag='%s'\n",
			bind_rc, ldap_result_name(bind_rc), bind_diag);
	else if (!search_ok)
		carglog(carg, L_ERROR,
			"ldap: SearchResultDone resultCode=%"PRId64" (%s) diag='%s' "
			"(bind OK; check ACL on cn=Monitor for the bind DN, and that database monitor is enabled)\n",
			search_rc, ldap_result_name(search_rc), search_diag);

	carglog(carg, L_INFO,
		"ldap parser: bind_ok=%d search_ok=%d bind_rc=%"PRId64" search_rc=%"PRId64" entries=%d status=%d\n",
		bind_ok, search_ok, bind_rc, search_rc, entries, carg->parser_status);
}

/* Harden TLS defaults for password binds (same idea as action tls_ca → verify). */
void ldap_carg_secure(context_arg *carg, host_aggregator_info *hi)
{
	int has_pass;

	if (!carg || !hi)
		return;

	has_pass = hi->pass && hi->pass[0];

	if (carg->tls && !carg->tls_verify_defined) {
		/* Prefer verifying the peer when LDAPS is used with a CA or a password. */
		if (carg->tls_ca_file || has_pass) {
			carg->tls_verify_defined = 1;
			carg->tls_verify = 1;
			carglog(carg, L_INFO,
				"ldap: tls_verify enabled by default for LDAPS (set tls_verify=off to disable)\n");
		}
	}
}

string *ldap_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	ldap_buf out = {0};
	const char *dn = "";
	const char *pass = "";
	const char *base = "cn=Monitor";

	(void)arg;
	(void)proxy_settings;

	if (!hi)
		return NULL;
	/* tcp://, tls:// (LDAPS), unix:// (ldapi) */
	if (hi->transport != APROTO_TCP && hi->transport != APROTO_TLS &&
		hi->transport != APROTO_UNIX)
		return NULL;

	if (hi->user && hi->user[0])
		dn = hi->user;
	if (hi->pass)
		pass = hi->pass;
	/* Optional path as base DN: tcp://host:389/cn=Monitor */
	if (hi->query && hi->query[0])
		base = hi->query;

	glog(L_INFO, "ldap mesg: base='%s' bind_dn='%s' tls=%d transport=%d\n",
		base, dn[0] ? dn : "(anonymous)", hi->tls, hi->transport);

	if (ldap_build_bind(&out, dn, pass)) {
		free(out.buf);
		return NULL;
	}
	if (ldap_build_search(&out, base)) {
		free(out.buf);
		return NULL;
	}

	/* +1: string_init_add writes a NUL at s[len]. */
	if (ldap_buf_reserve(&out, 1)) {
		free(out.buf);
		return NULL;
	}
	out.buf[out.len] = 0;
	return string_init_add((char *)out.buf, out.len, out.len);
}

void ldap_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("ldap");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler) * actx->handlers);
	actx->handler[0].name = ldap_handler;
	actx->handler[0].validator = ldap_validator;
	actx->handler[0].mesg_func = ldap_mesg;
	strlcpy(actx->handler[0].key, "ldap", 255);
	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
