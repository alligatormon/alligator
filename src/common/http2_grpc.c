#include "common/http2_grpc.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "common/selector.h"
#include "common/logs.h"
#include "events/context_arg.h"
#include "events/tls.h"
#include "main.h"

#define H2_FRAME_DATA 0x0
#define H2_FRAME_HEADERS 0x1
#define H2_FRAME_RST_STREAM 0x3
#define H2_FRAME_SETTINGS 0x4
#define H2_FRAME_PING 0x6
#define H2_FRAME_GOAWAY 0x7
#define H2_FRAME_WINDOW_UPDATE 0x8
#define H2_FRAME_CONTINUATION 0x9

#define H2_FLAG_END_STREAM 0x1
#define H2_FLAG_ACK 0x1
#define H2_FLAG_END_HEADERS 0x4
#define H2_FLAG_PADDED 0x8
#define H2_FLAG_PRIORITY 0x20

static const char h2_preface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

/* RFC 7541 Appendix B — Huffman code lengths and codes (MSB first). */
static const uint8_t huff_bits[257] = {
	13,23,28,28,28,28,28,28,28,24,30,28,28,30,28,28,28,28,28,28,28,28,30,28,
	28,28,28,28,28,28,28,28, 6,10,10,12,13, 6, 8,11,10,10, 8,11, 8, 6, 6, 6,
	 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 7, 8,15, 6,12,10,13, 6, 9, 9, 9, 7, 7,
	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8, 7,
	 8,13,19,13,14, 6,15, 5, 6, 5, 6, 5, 6, 6, 6, 5, 7, 7, 6, 6, 6, 5,
	 6, 7, 6, 5, 5, 6, 7, 7, 7,15,11,14,13,28,20,22,20,20,22,22,22,23,22,
	23,23,23,23,23,24,23,24,24,22,23,24,23,23,23,23,21,22,23,22,23,23,24,22,
	21,20,22,22,23,23,21,23,22,22,24,21,22,23,23,21,21,22,21,23,22,23,23,20,
	22,21,20,20,21,20,21,21,21,21,21,21,19,21,22,21,21,21,21,21,21,21,21,21,
	21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,
	21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,30
};
static const uint32_t huff_codes[257] = {
	0x1ff8,0x7fffd8,0xfffffe2,0xfffffe3,0xfffffe4,0xfffffe5,0xfffffe6,0xfffffe7,
	0xfffffe8,0xffffea,0x3ffffffc,0xfffffe9,0xfffffea,0x3ffffffd,0xfffffeb,0xfffffec,
	0xfffffed,0xfffffee,0xfffffef,0xffffff0,0xffffff1,0xffffff2,0x3ffffffe,0xffffff3,
	0xffffff4,0xffffff5,0xffffff6,0xffffff7,0xffffff8,0xffffff9,0xffffffa,0xffffffb,
	0x14,0x3f8,0x3f9,0xffa,0x1ff9,0x15,0xf8,0x7fa,0x3fa,0x3fb,0xf9,0x7fb,0xfa,0x16,0x17,0x18,
	0x0,0x1,0x2,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x5c,0xfb,0x7ffc,0x20,0xffb,0x3fc,0x1ffa,0x21,
	0x5d,0x5e,0x5f,0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x6b,0x6c,0x6d,0x6e,
	0x6f,0x70,0x71,0x72,0xfc,0x73,0xfd,0x1ffb,0x7fff0,0x1ffc,0x3ffc,0x22,0x7ffd,0x3,0x23,0x4,
	0x24,0x5,0x25,0x26,0x27,0x6,0x74,0x75,0x28,0x29,0x2a,0x7,0x2b,0x76,0x2c,0x8,0x9,0x2d,0x77,
	0x78,0x79,0x7ffe,0x7fc,0x3ffd,0x1ffd,0xffffffc,0xfffe6,0x3fffd2,0xfffe7,0xfffe8,0x3fffd3,
	0x3fffd4,0x3fffd5,0x7fffd9,0x3fffd6,0x7fffda,0x7fffdb,0x7fffdc,0x7fffdd,0x7fffde,0xffffeb,
	0x7fffdf,0xffffec,0xffffed,0x3fffd7,0x7fffe0,0xffffee,0x7fffe1,0x7fffe2,0x7fffe3,0x7fffe4,
	0x1fffdc,0x3fffd8,0x7fffe5,0x3fffd9,0x7fffe6,0x7fffe7,0xffffef,0x3fffda,0x1fffdd,0xfffe9,
	0x3fffdb,0x3fffdc,0x7fffe8,0x7fffe9,0x1fffde,0x7fffea,0x3fffdd,0x3fffde,0xfffff0,0x1fffdf,
	0x3fffdf,0x7fffeb,0x7fffec,0x1fffe0,0x1fffe1,0x3fffe0,0x1fffe2,0x7fffed,0x3fffe1,0x7fffee,
	0x7fffef,0xfffea,0x3fffe2,0x3fffe3,0x3fffe4,0x7ffff0,0x3fffe5,0x3fffe6,0x7ffff1,0x3ffffe0,
	0x3ffffe1,0xfffeb,0x7fff1,0x3fffe7,0x7ffff2,0x3fffe8,0x1ffffec,0x3ffffe2,0x3ffffe3,0x3ffffe4,
	0x7ffffde,0x7ffffdf,0x3ffffe5,0xfffff1,0x1ffffed,0x7fff2,0x1fffe3,0x3ffffe6,0x7ffffe0,
	0x7ffffe1,0x3ffffe7,0x7ffffe2,0xfffff2,0x1fffe4,0x1fffe5,0x3ffffe8,0x3ffffe9,0xffffffd,
	0x7ffffe3,0x7ffffe4,0x7ffffe5,0xfffec,0xfffff3,0xfffed,0x1fffe6,0x3fffe9,0x1fffe7,0x1fffe8,
	0x7ffff3,0x3fffea,0x3fffeb,0x1ffffee,0x1ffffef,0xfffff4,0xfffff5,0x3ffffea,0x7ffff4,0x3ffffeb,
	0x7ffffe6,0x3ffffec,0x3ffffed,0x7ffffe7,0x7ffffe8,0x7ffffe9,0x7ffffea,0x7ffffeb,0xffffffe,
	0x7ffffec,0x7ffffed,0x7ffffee,0x7ffffef,0x7fffff0,0x3ffffee,0x3fffffff
};

string *otlp_grpc_frame_wrap(const char *protobuf, size_t protobuf_len)
{
	string *out;
	uint32_t be;

	if (!protobuf && protobuf_len)
		return NULL;

	out = string_init(5 + protobuf_len);
	if (!out)
		return NULL;

	out->s[out->l++] = 0; /* compress flag */
	be = (uint32_t)protobuf_len;
	out->s[out->l++] = (char)((be >> 24) & 0xff);
	out->s[out->l++] = (char)((be >> 16) & 0xff);
	out->s[out->l++] = (char)((be >> 8) & 0xff);
	out->s[out->l++] = (char)(be & 0xff);
	if (protobuf_len && protobuf)
		string_cat(out, (char *)protobuf, protobuf_len);
	return out;
}

const char *otlp_grpc_frame_unwrap(const char *frame, size_t frame_len, size_t *out_len)
{
	uint32_t msg_len;

	if (out_len)
		*out_len = 0;
	if (!frame || frame_len < 5)
		return NULL;
	if ((unsigned char)frame[0] != 0)
		return NULL; /* compressed — not supported in MVP */

	msg_len = ((uint32_t)(unsigned char)frame[1] << 24)
		| ((uint32_t)(unsigned char)frame[2] << 16)
		| ((uint32_t)(unsigned char)frame[3] << 8)
		| ((uint32_t)(unsigned char)frame[4]);
	if ((size_t)msg_len + 5 > frame_len)
		return NULL;
	if (out_len)
		*out_len = msg_len;
	return frame + 5;
}

static void h2_put_u24(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)((v >> 16) & 0xff);
	p[1] = (uint8_t)((v >> 8) & 0xff);
	p[2] = (uint8_t)(v & 0xff);
}

static void h2_put_u32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)((v >> 24) & 0xff);
	p[1] = (uint8_t)((v >> 16) & 0xff);
	p[2] = (uint8_t)((v >> 8) & 0xff);
	p[3] = (uint8_t)(v & 0xff);
}

static uint32_t h2_get_u24(const uint8_t *p)
{
	return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
}

static uint32_t h2_get_u32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
		| ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void h2_append_frame(string *out, uint8_t type, uint8_t flags, uint32_t stream_id, const void *payload, size_t payload_len)
{
	uint8_t hdr[9];

	h2_put_u24(hdr, (uint32_t)payload_len);
	hdr[3] = type;
	hdr[4] = flags;
	h2_put_u32(hdr + 5, stream_id & 0x7fffffffu);
	string_cat(out, (char *)hdr, 9);
	if (payload_len && payload)
		string_cat(out, (char *)payload, payload_len);
}

static void hpack_encode_int(string *out, uint32_t value, uint8_t prefix_bits, uint8_t first_byte_hi)
{
	uint32_t max = (1u << prefix_bits) - 1u;
	uint8_t b;

	if (value < max) {
		b = (uint8_t)(first_byte_hi | value);
		string_cat(out, (char *)&b, 1);
		return;
	}
	b = (uint8_t)(first_byte_hi | max);
	string_cat(out, (char *)&b, 1);
	value -= max;
	while (value >= 128) {
		b = (uint8_t)((value & 0x7f) | 0x80);
		string_cat(out, (char *)&b, 1);
		value >>= 7;
	}
	b = (uint8_t)value;
	string_cat(out, (char *)&b, 1);
}

static void hpack_encode_string_raw(string *out, const char *s, size_t len)
{
	hpack_encode_int(out, (uint32_t)len, 7, 0); /* H=0 raw */
	if (len && s)
		string_cat(out, (char *)s, len);
}

static void hpack_literal_new_name(string *out, const char *name, size_t nlen, const char *value, size_t vlen)
{
	/* Literal Header Field without Indexing — New Name (0x00) */
	string_cat(out, "\x00", 1);
	hpack_encode_string_raw(out, name, nlen);
	hpack_encode_string_raw(out, value, vlen);
}

static void hpack_indexed(string *out, uint32_t index)
{
	hpack_encode_int(out, index, 7, 0x80);
}

static int hpack_decode_int(const uint8_t **pp, const uint8_t *end, uint8_t prefix_bits, uint32_t *out)
{
	const uint8_t *p = *pp;
	uint32_t max;
	uint32_t value;
	uint32_t m;
	uint8_t b;

	if (p >= end || prefix_bits == 0 || prefix_bits > 8)
		return 0;
	max = (1u << prefix_bits) - 1u;
	value = (*p) & max;
	p++;
	if (value < max) {
		*out = value;
		*pp = p;
		return 1;
	}
	m = 0;
	while (p < end) {
		b = *p++;
		value += ((uint32_t)(b & 0x7f)) << m;
		m += 7;
		if (!(b & 0x80)) {
			*out = value;
			*pp = p;
			return 1;
		}
		if (m > 28)
			return 0;
	}
	return 0;
}

static int huff_decode(const uint8_t *src, size_t srclen, char *dst, size_t dstmax, size_t *outlen)
{
	uint64_t acc = 0;
	uint8_t bits = 0;
	size_t o = 0;
	size_t i;

	for (i = 0; i < srclen; i++) {
		acc = (acc << 8) | src[i];
		bits += 8;
		while (bits >= 5) {
			int matched = 0;
			int sym;
			for (sym = 0; sym < 257; sym++) {
				uint8_t n = huff_bits[sym];
				uint32_t code;
				if (n > bits)
					continue;
				code = (uint32_t)((acc >> (bits - n)) & ((n == 32) ? 0xffffffffu : ((1u << n) - 1u)));
				if (code != huff_codes[sym])
					continue;
				bits = (uint8_t)(bits - n);
				if (sym == 256) {
					*outlen = o;
					return 1;
				}
				if (o >= dstmax)
					return 0;
				dst[o++] = (char)sym;
				matched = 1;
				break;
			}
			if (!matched)
				break;
		}
	}
	/* EOS padding: remaining bits must be all ones (RFC 7541 §5.2) */
	if (bits) {
		uint32_t pad = (uint32_t)((acc) & ((1u << bits) - 1u));
		uint32_t ones = (bits == 32) ? 0xffffffffu : ((1u << bits) - 1u);
		if (pad != ones)
			return 0;
	}
	*outlen = o;
	return 1;
}

static int hpack_decode_string(const uint8_t **pp, const uint8_t *end, char *dst, size_t dstmax, size_t *outlen)
{
	const uint8_t *p = *pp;
	uint32_t slen = 0;
	int huffman;
	size_t decoded = 0;

	if (p >= end)
		return 0;
	huffman = (*p & 0x80) ? 1 : 0;
	if (!hpack_decode_int(&p, end, 7, &slen))
		return 0;
	if (p + slen > end)
		return 0;
	if (huffman) {
		if (!huff_decode(p, slen, dst, dstmax, &decoded))
			return 0;
	} else {
		if (slen >= dstmax)
			return 0;
		memcpy(dst, p, slen);
		decoded = slen;
	}
	dst[decoded] = '\0';
	*outlen = decoded;
	*pp = p + slen;
	return 1;
}

/* Minimal static table subset used when decoding peer response headers. */
static const char *hpack_static_name(uint32_t idx)
{
	switch (idx) {
	case 1: return ":authority";
	case 2: return ":method";
	case 3: return ":method";
	case 4: return ":path";
	case 5: return ":path";
	case 6: return ":scheme";
	case 7: return ":scheme";
	case 8: return ":status";
	case 9: return ":status";
	case 10: return ":status";
	case 11: return ":status";
	case 12: return ":status";
	case 13: return ":status";
	case 14: return ":status";
	case 15: return "accept-charset";
	case 16: return "accept-encoding";
	case 17: return "accept-language";
	case 18: return "accept-ranges";
	case 19: return "accept";
	case 20: return "access-control-allow-origin";
	case 21: return "age";
	case 22: return "allow";
	case 23: return "authorization";
	case 24: return "cache-control";
	case 25: return "content-disposition";
	case 26: return "content-encoding";
	case 27: return "content-language";
	case 28: return "content-length";
	case 29: return "content-location";
	case 30: return "content-range";
	case 31: return "content-type";
	case 32: return "cookie";
	case 33: return "date";
	case 34: return "etag";
	case 35: return "expect";
	case 36: return "expires";
	case 37: return "from";
	case 38: return "host";
	case 39: return "if-match";
	case 40: return "if-modified-since";
	case 41: return "if-none-match";
	case 42: return "if-range";
	case 43: return "if-unmodified-since";
	case 44: return "last-modified";
	case 45: return "link";
	case 46: return "location";
	case 47: return "max-forwards";
	case 48: return "proxy-authenticate";
	case 49: return "proxy-authorization";
	case 50: return "range";
	case 51: return "referer";
	case 52: return "refresh";
	case 53: return "retry-after";
	case 54: return "server";
	case 55: return "set-cookie";
	case 56: return "strict-transport-security";
	case 57: return "transfer-encoding";
	case 58: return "user-agent";
	case 59: return "vary";
	case 60: return "via";
	case 61: return "www-authenticate";
	default: return NULL;
	}
}

static const char *hpack_static_value(uint32_t idx)
{
	switch (idx) {
	case 2: return "GET";
	case 3: return "POST";
	case 4: return "/";
	case 5: return "/index.html";
	case 6: return "http";
	case 7: return "https";
	case 8: return "200";
	case 9: return "204";
	case 10: return "206";
	case 11: return "304";
	case 12: return "400";
	case 13: return "404";
	case 14: return "500";
	case 16: return "gzip, deflate";
	default: return "";
	}
}

typedef void (*hpack_header_cb)(void *arg, const char *name, size_t nlen, const char *value, size_t vlen);

/* Minimal decoder-side dynamic table (RFC 7541). Envoy/Istio often insert
 * content-type then reference later names by dynamic index; without this,
 * grpc-status trailers can be skipped and surface as grpc-status=-1. */
#define HPACK_DYN_MAX 64
#define HPACK_STATIC_ENTRIES 61

typedef struct {
	char name[128];
	char value[256];
	size_t nlen;
	size_t vlen;
} hpack_dyn_entry;

typedef struct {
	hpack_dyn_entry e[HPACK_DYN_MAX];
	size_t n;
} hpack_dtable;

static void hpack_dtable_insert(hpack_dtable *dt, const char *name, size_t nlen, const char *value, size_t vlen)
{
	if (!dt || !name || nlen >= sizeof(dt->e[0].name) || vlen >= sizeof(dt->e[0].value))
		return;
	if (dt->n >= HPACK_DYN_MAX)
		dt->n = HPACK_DYN_MAX - 1; /* drop oldest (highest index) */
	if (dt->n > 0)
		memmove(&dt->e[1], &dt->e[0], dt->n * sizeof(dt->e[0]));
	memcpy(dt->e[0].name, name, nlen);
	dt->e[0].name[nlen] = '\0';
	dt->e[0].nlen = nlen;
	if (vlen && value)
		memcpy(dt->e[0].value, value, vlen);
	dt->e[0].value[vlen] = '\0';
	dt->e[0].vlen = vlen;
	dt->n++;
}

static int hpack_lookup(hpack_dtable *dt, uint32_t idx, const char **name, size_t *nlen, const char **value, size_t *vlen)
{
	if (!idx)
		return 0;
	if (idx <= HPACK_STATIC_ENTRIES) {
		const char *sn = hpack_static_name(idx);
		const char *sv = hpack_static_value(idx);
		if (!sn)
			return 0;
		*name = sn;
		*nlen = strlen(sn);
		*value = sv ? sv : "";
		*vlen = sv ? strlen(sv) : 0;
		return 1;
	}
	if (!dt)
		return 0;
	{
		uint32_t di = idx - HPACK_STATIC_ENTRIES - 1;
		if (di >= dt->n)
			return 0;
		*name = dt->e[di].name;
		*nlen = dt->e[di].nlen;
		*value = dt->e[di].value;
		*vlen = dt->e[di].vlen;
		return 1;
	}
}

static int hpack_decode_block(const uint8_t *block, size_t block_len, hpack_header_cb cb, void *arg, hpack_dtable *dt)
{
	const uint8_t *p = block;
	const uint8_t *end = block + block_len;
	char name[256];
	char value[1024];
	size_t nlen = 0;
	size_t vlen = 0;

	while (p < end) {
		uint8_t b = *p;
		uint32_t idx = 0;

		if (b & 0x80) {
			const char *nn = NULL;
			const char *vv = NULL;
			size_t nnlen = 0;
			size_t vvlen = 0;
			if (!hpack_decode_int(&p, end, 7, &idx))
				return 0;
			if (!hpack_lookup(dt, idx, &nn, &nnlen, &vv, &vvlen))
				return 0;
			if (cb)
				cb(arg, nn, nnlen, vv, vvlen);
			continue;
		}
		if ((b & 0xc0) == 0x40) {
			/* Literal with incremental indexing */
			if (!hpack_decode_int(&p, end, 6, &idx))
				return 0;
			if (idx == 0) {
				if (!hpack_decode_string(&p, end, name, sizeof(name), &nlen))
					return 0;
			} else {
				const char *nn = NULL;
				const char *vv = NULL;
				size_t nnlen = 0;
				size_t vvlen = 0;
				if (!hpack_lookup(dt, idx, &nn, &nnlen, &vv, &vvlen) || !nn)
					return 0;
				if (nnlen >= sizeof(name))
					return 0;
				memcpy(name, nn, nnlen + 1);
				nlen = nnlen;
				(void)vv;
				(void)vvlen;
			}
			if (!hpack_decode_string(&p, end, value, sizeof(value), &vlen))
				return 0;
			/* Must insert or fail: skipping would desync later indexed refs. */
			if (!dt || nlen >= sizeof(dt->e[0].name) || vlen >= sizeof(dt->e[0].value))
				return 0;
			hpack_dtable_insert(dt, name, nlen, value, vlen);
			if (cb)
				cb(arg, name, nlen, value, vlen);
			continue;
		}
		if ((b & 0xe0) == 0x20) {
			/* Dynamic table size update — ignore value (MVP keeps a fixed cap). */
			if (!hpack_decode_int(&p, end, 5, &idx))
				return 0;
			(void)idx;
			continue;
		}
		/* Literal without indexing / never indexed */
		{
			uint8_t prefix = 4;
			if (!hpack_decode_int(&p, end, prefix, &idx))
				return 0;
			if (idx == 0) {
				if (!hpack_decode_string(&p, end, name, sizeof(name), &nlen))
					return 0;
			} else {
				const char *nn = NULL;
				const char *vv = NULL;
				size_t nnlen = 0;
				size_t vvlen = 0;
				if (!hpack_lookup(dt, idx, &nn, &nnlen, &vv, &vvlen) || !nn)
					return 0;
				if (nnlen >= sizeof(name))
					return 0;
				memcpy(name, nn, nnlen + 1);
				nlen = nnlen;
				(void)vv;
				(void)vvlen;
			}
			if (!hpack_decode_string(&p, end, value, sizeof(value), &vlen))
				return 0;
			if (cb)
				cb(arg, name, nlen, value, vlen);
		}
	}
	return 1;
}

typedef struct {
	string *block;
	alligator_ht *env;
} hpack_build_arg;

static void hpack_env_foreach(void *funcarg, void *arg)
{
	hpack_build_arg *ba = funcarg;
	env_struct *es = arg;
	char name_lower[256];
	size_t i;
	size_t nlen;

	if (!ba || !ba->block || !es || !es->k || !es->v)
		return;
	/* Skip headers we set ourselves */
	if (!strcasecmp(es->k, "content-type") || !strcasecmp(es->k, "content-length")
		|| !strcasecmp(es->k, "te") || !strcasecmp(es->k, "host")
		|| !strcasecmp(es->k, "connection") || !strcasecmp(es->k, "transfer-encoding")
		|| !strcasecmp(es->k, "user-agent"))
		return;

	nlen = strlen(es->k);
	if (nlen >= sizeof(name_lower))
		return;
	for (i = 0; i < nlen; i++)
		name_lower[i] = (char)tolower((unsigned char)es->k[i]);
	name_lower[nlen] = '\0';
	hpack_literal_new_name(ba->block, name_lower, nlen, es->v, strlen(es->v));
}

char *otlp_grpc_http2_request_build(const char *authority, const char *path,
	int scheme_https, const char *grpc_framed, size_t grpc_framed_len,
	alligator_ht *env, size_t *out_len)
{
	string *out;
	string *hdrs;
	char *ret;
	const char *use_path = path;

	if (out_len)
		*out_len = 0;
	if (!authority || !grpc_framed)
		return NULL;
	/* Defense in depth: action.c also refuses; without send flow-control a
	 * framed body over the default 65535 connection window is unsafe. */
	if (grpc_framed_len > 65535)
		return NULL;
	if (!use_path || !*use_path || !strcmp(use_path, "/"))
		use_path = OTLP_GRPC_METRICS_PATH;

	out = string_init(256 + grpc_framed_len);
	hdrs = string_init(512);
	if (!out || !hdrs) {
		string_free(out);
		string_free(hdrs);
		return NULL;
	}

	string_cat(out, (char *)h2_preface, sizeof(h2_preface) - 1);

	/* SETTINGS: INITIAL_WINDOW_SIZE=1<<20, MAX_FRAME_SIZE=16384 */
	{
		uint8_t sp[16];
		h2_put_u32(sp + 0, 0x4);
		h2_put_u32(sp + 4, 1u << 20);
		h2_put_u32(sp + 8, 0x5);
		h2_put_u32(sp + 12, 16384);
		h2_append_frame(out, H2_FRAME_SETTINGS, 0, 0, sp, 16);
	}

	/* HEADERS for stream 1 */
	hpack_indexed(hdrs, 3); /* :method POST */
	if (scheme_https)
		hpack_indexed(hdrs, 7); /* :scheme https */
	else
		hpack_indexed(hdrs, 6); /* :scheme http */
	hpack_literal_new_name(hdrs, ":path", 5, use_path, strlen(use_path));
	hpack_literal_new_name(hdrs, ":authority", 10, authority, strlen(authority));
	hpack_literal_new_name(hdrs, "content-type", 12, "application/grpc", 16);
	hpack_literal_new_name(hdrs, "te", 2, "trailers", 8);
	hpack_literal_new_name(hdrs, "user-agent", 10, "alligator", 9);
	if (env) {
		hpack_build_arg ba = { .block = hdrs, .env = env };
		alligator_ht_foreach_arg(env, hpack_env_foreach, &ba);
	}

	h2_append_frame(out, H2_FRAME_HEADERS, H2_FLAG_END_HEADERS, 1, hdrs->s, hdrs->l);
	/* Split DATA to peer MAX_FRAME_SIZE (we advertise 16384). */
	{
		size_t off = 0;
		const size_t max_frame = 16384;
		while (off < grpc_framed_len) {
			size_t chunk = grpc_framed_len - off;
			uint8_t flags = 0;
			if (chunk > max_frame)
				chunk = max_frame;
			if (off + chunk >= grpc_framed_len)
				flags = H2_FLAG_END_STREAM;
			h2_append_frame(out, H2_FRAME_DATA, flags, 1, grpc_framed + off, chunk);
			off += chunk;
		}
		if (grpc_framed_len == 0)
			h2_append_frame(out, H2_FRAME_DATA, H2_FLAG_END_STREAM, 1, NULL, 0);
	}

	string_free(hdrs);
	ret = out->s;
	if (out_len)
		*out_len = out->l;
	free(out);
	return ret;
}

typedef struct {
	uv_write_t req;
	uv_buf_t buf;
	char data[17]; /* SETTINGS ACK (9) or PING ACK (17) */
} h2_ack_write_t;

static void h2_ack_written(uv_write_t *req, int status)
{
	(void)status;
	free(req);
}

static int h2_write_control(context_arg *carg, const char *frame, size_t frame_len)
{
	h2_ack_write_t *w;

	if (!carg || !frame || !frame_len || frame_len > sizeof(((h2_ack_write_t *)0)->data))
		return -1;

	/*
	 * Never use tls_write() here: it reuses carg->write_tls / write_buffer and
	 * may run while the oneshot request write is still in flight. Encrypt into
	 * the SSL BIO and flush with independent uv_write reqs instead.
	 */
	if (carg->tls) {
		size_t written = 0;

		if (!carg->ssl)
			return -1;
		if (SSL_write_ex(carg->ssl, frame, frame_len, &written) != 1 || written != frame_len)
			return -1;
		flush_tls_write(carg, (uv_stream_t *)&carg->client);
		return 0;
	}

	w = calloc(1, sizeof(*w));
	if (!w)
		return -1;
	memcpy(w->data, frame, frame_len);
	w->buf = uv_buf_init(w->data, (unsigned int)frame_len);
	w->req.data = carg;
	if (uv_write(&w->req, (uv_stream_t *)&carg->client, &w->buf, 1, h2_ack_written) < 0) {
		free(w);
		return -1;
	}
	return 0;
}

static void h2_send_settings_ack(context_arg *carg)
{
	uint8_t frame[9];

	if (!carg || carg->http2_settings_ack_sent)
		return;

	h2_put_u24(frame, 0);
	frame[3] = H2_FRAME_SETTINGS;
	frame[4] = H2_FLAG_ACK;
	h2_put_u32(frame + 5, 0);
	if (h2_write_control(carg, (char *)frame, 9) == 0)
		carg->http2_settings_ack_sent = 1;
}

static void h2_send_ping_ack(context_arg *carg, const uint8_t *payload, size_t plen)
{
	uint8_t frame[17];

	if (!carg || !payload || plen != 8)
		return;

	h2_put_u24(frame, 8);
	frame[3] = H2_FRAME_PING;
	frame[4] = H2_FLAG_ACK;
	h2_put_u32(frame + 5, 0);
	memcpy(frame + 9, payload, 8);
	h2_write_control(carg, (char *)frame, 17);
}

#define H2_END_NONE 0
#define H2_END_HEADERS 1
#define H2_END_DATA 2
#define H2_END_GOAWAY 3
#define H2_END_RST 4

typedef struct {
	int grpc_status;
	int have_status;
	char status_msg[256];
	char http_status[16];
	string *data;
	int stream_ended;
	int headers_end;
	int end_reason;
	uint32_t goaway_last_sid;
	uint32_t goaway_error;
	uint32_t rst_error;
	int saw_headers;
	int saw_data;
	int saw_settings;
	int saw_goaway;
	int saw_rst;
	int hpack_ok;
	hpack_dtable dtable;
} h2_resp_state;

static void h2_hdr_cb(void *arg, const char *name, size_t nlen, const char *value, size_t vlen)
{
	h2_resp_state *st = arg;
	(void)nlen;
	(void)vlen;
	if (!st || !name)
		return;
	if (!strcasecmp(name, "grpc-status")) {
		st->grpc_status = atoi(value ? value : "0");
		st->have_status = 1;
	} else if (!strcasecmp(name, "grpc-message") && value) {
		strlcpy(st->status_msg, value, sizeof(st->status_msg));
	} else if (!strcasecmp(name, ":status") && value) {
		strlcpy(st->http_status, value, sizeof(st->http_status));
	}
}

static const char *h2_frame_type_name(uint8_t type)
{
	switch (type) {
	case H2_FRAME_DATA: return "DATA";
	case H2_FRAME_HEADERS: return "HEADERS";
	case H2_FRAME_RST_STREAM: return "RST_STREAM";
	case H2_FRAME_SETTINGS: return "SETTINGS";
	case H2_FRAME_PING: return "PING";
	case H2_FRAME_GOAWAY: return "GOAWAY";
	case H2_FRAME_WINDOW_UPDATE: return "WINDOW_UPDATE";
	case H2_FRAME_CONTINUATION: return "CONTINUATION";
	default: return "OTHER";
	}
}

/* RFC 7540 §7 — keep short for status_msg budgets. */
static const char *h2_error_name(uint32_t code)
{
	switch (code) {
	case 0: return "NO_ERROR";
	case 1: return "PROTOCOL_ERROR";
	case 2: return "INTERNAL_ERROR";
	case 3: return "FLOW_CONTROL_ERROR";
	case 4: return "SETTINGS_TIMEOUT";
	case 5: return "STREAM_CLOSED";
	case 6: return "FRAME_SIZE_ERROR";
	case 7: return "REFUSED_STREAM";
	case 8: return "CANCEL";
	case 9: return "COMPRESSION_ERROR";
	case 10: return "CONNECT_ERROR";
	case 11: return "ENHANCE_YOUR_CALM";
	case 12: return "INADEQUATE_SECURITY";
	case 13: return "HTTP_1_1_REQUIRED";
	default: return "UNKNOWN";
	}
}

static const char *h2_end_reason_name(int reason)
{
	switch (reason) {
	case H2_END_HEADERS: return "HEADERS";
	case H2_END_DATA: return "DATA";
	case H2_END_GOAWAY: return "GOAWAY";
	case H2_END_RST: return "RST_STREAM";
	default: return "NONE";
	}
}

/* Build a short frame inventory for logs (binary H2 must not use %s). */
static void h2_summarize_frames(const uint8_t *buf, size_t len, char *out, size_t outsz)
{
	size_t off = 0;
	size_t used = 0;

	if (!out || !outsz)
		return;
	out[0] = '\0';
	while (off + 9 <= len && used + 24 < outsz) {
		uint32_t plen = h2_get_u24(buf + off);
		uint8_t type = buf[off + 3];
		uint8_t flags = buf[off + 4];
		uint32_t sid = h2_get_u32(buf + off + 5) & 0x7fffffffu;
		int n;

		if (off + 9 + plen > len) {
			n = snprintf(out + used, outsz - used, "%s%s(trunc plen=%u)",
				used ? " " : "", h2_frame_type_name(type), plen);
			if (n > 0)
				used += (size_t)n;
			break;
		}
		n = snprintf(out + used, outsz - used, "%s%s(len=%u fl=0x%02x sid=%u)",
			used ? " " : "", h2_frame_type_name(type), plen, flags, sid);
		if (n > 0)
			used += (size_t)n;
		off += 9 + plen;
	}
	if (off < len && used + 16 < outsz)
		snprintf(out + used, outsz - used, " +%zuB", len - off);
}

/* Parse HTTP/2 frames. When start_off is set (expect_complete path), only
 * frames at/after *start_off get SETTINGS/PING ACKs; *start_off advances
 * contiguously so control frames are not re-ACKed on every read. SETTINGS is
 * not committed until http2_settings_ack_sent so a failed ACK is retried.
 * Full response parse passes start_off=NULL and collects DATA into st->data. */
static int h2_feed_frames(const uint8_t *buf, size_t len, h2_resp_state *st,
	context_arg *carg_for_ack, size_t *start_off)
{
	size_t off = 0;
	size_t commit = start_off ? *start_off : 0;

	while (off + 9 <= len) {
		uint32_t plen = h2_get_u24(buf + off);
		uint8_t type = buf[off + 3];
		uint8_t flags = buf[off + 4];
		uint32_t sid = h2_get_u32(buf + off + 5) & 0x7fffffffu;
		const uint8_t *payload;
		size_t poff = 0;
		size_t next;
		int do_ack;

		if (off + 9 + plen > len)
			break; /* need more */

		payload = buf + off + 9;
		next = off + 9 + plen;
		do_ack = (carg_for_ack && off >= commit);

		if (type == H2_FRAME_SETTINGS && sid == 0) {
			st->saw_settings = 1;
			if (do_ack && !(flags & H2_FLAG_ACK))
				h2_send_settings_ack(carg_for_ack);
		} else if (type == H2_FRAME_PING && sid == 0) {
			if (do_ack && !(flags & H2_FLAG_ACK) && plen == 8)
				h2_send_ping_ack(carg_for_ack, payload, plen);
		} else if (type == H2_FRAME_WINDOW_UPDATE) {
			/* MVP: ignore peer window updates; we do not track send window. */
		} else if (type == H2_FRAME_GOAWAY) {
			st->saw_goaway = 1;
			if (plen >= 8) {
				st->goaway_last_sid = h2_get_u32(payload) & 0x7fffffffu;
				st->goaway_error = h2_get_u32(payload + 4);
			}
			/* last_stream_id < 1 ⇒ peer will not process stream 1. A drain
			 * GOAWAY with last_stream_id >= 1 must not truncate a live stream
			 * or overwrite a successful END_STREAM. */
			if (st->goaway_last_sid < 1 && st->end_reason == H2_END_NONE) {
				st->stream_ended = 1;
				st->end_reason = H2_END_GOAWAY;
			}
		} else if (type == H2_FRAME_RST_STREAM && sid == 1) {
			st->saw_rst = 1;
			st->stream_ended = 1;
			if (st->end_reason == H2_END_NONE)
				st->end_reason = H2_END_RST;
			if (plen >= 4)
				st->rst_error = h2_get_u32(payload);
		} else if ((type == H2_FRAME_HEADERS || type == H2_FRAME_CONTINUATION) && sid == 1) {
			const uint8_t *block = payload;
			size_t blen = plen;

			st->saw_headers = 1;
			if (type == H2_FRAME_HEADERS) {
				if (flags & H2_FLAG_PADDED) {
					if (blen < 1)
						return 0;
					{
						uint8_t pad = payload[0];
						poff = 1;
						if (blen < poff + pad)
							return 0;
						blen -= poff + pad;
						block = payload + poff;
					}
				}
				if (flags & H2_FLAG_PRIORITY) {
					if (blen < 5)
						return 0;
					block += 5;
					blen -= 5;
				}
			}
			if (!hpack_decode_block(block, blen, h2_hdr_cb, st, &st->dtable))
				st->hpack_ok = 0;
			if (flags & H2_FLAG_END_HEADERS)
				st->headers_end = 1;
			if (flags & H2_FLAG_END_STREAM) {
				st->stream_ended = 1;
				if (st->end_reason == H2_END_NONE)
					st->end_reason = H2_END_HEADERS;
			}
		} else if (type == H2_FRAME_DATA && sid == 1) {
			const uint8_t *data = payload;
			size_t dlen = plen;
			st->saw_data = 1;
			if (flags & H2_FLAG_PADDED) {
				if (dlen < 1)
					return 0;
				{
					uint8_t pad = payload[0];
					if (dlen < 1u + pad)
						return 0;
					data = payload + 1;
					dlen = dlen - 1u - pad;
				}
			}
			if (dlen && st->data)
				string_cat(st->data, (char *)data, dlen);
			if (flags & H2_FLAG_END_STREAM) {
				st->stream_ended = 1;
				if (st->end_reason == H2_END_NONE)
					st->end_reason = H2_END_DATA;
			}
		}

		if (start_off && off == commit) {
			int block_commit = (type == H2_FRAME_SETTINGS && sid == 0
				&& !(flags & H2_FLAG_ACK) && carg_for_ack
				&& !carg_for_ack->http2_settings_ack_sent);
			if (!block_commit)
				commit = next;
		}

		off = next;
	}
	if (start_off)
		*start_off = commit;
	return 1;
}

int8_t otlp_grpc_expect_complete(context_arg *carg, char *buf, size_t nread)
{
	h2_resp_state st;
	(void)buf;
	(void)nread;

	if (!carg || !carg->full_body || !carg->full_body->s)
		return 0;

	memset(&st, 0, sizeof(st));
	st.grpc_status = -1;
	st.hpack_ok = 1;
	st.data = NULL;
	/* Side-effect only (ACKs + stream_ended); advance feed offset so PINGs are not re-ACKed. */
	h2_feed_frames((const uint8_t *)carg->full_body->s, carg->full_body->l, &st, carg,
		&carg->http2_feed_offset);

	if (st.stream_ended)
		return 1;
	return 0;
}

int otlp_grpc_http2_response_parse(const char *buf, size_t len,
	int *grpc_status, char *status_msg, size_t status_msg_size,
	char **protobuf_out, size_t *protobuf_len)
{
	h2_resp_state st;
	const char *msg;
	size_t msg_len = 0;
	char frames[256];

	if (grpc_status)
		*grpc_status = -1;
	if (status_msg && status_msg_size)
		status_msg[0] = '\0';
	if (protobuf_out)
		*protobuf_out = NULL;
	if (protobuf_len)
		*protobuf_len = 0;
	if (!buf)
		return 0;

	memset(&st, 0, sizeof(st));
	st.grpc_status = -1;
	st.hpack_ok = 1;
	st.data = string_init(256);
	if (!st.data)
		return 0;

	if (!h2_feed_frames((const uint8_t *)buf, len, &st, NULL, NULL)) {
		if (status_msg && status_msg_size) {
			h2_summarize_frames((const uint8_t *)buf, len, frames, sizeof(frames));
			snprintf(status_msg, status_msg_size,
				"HTTP/2 frame parse error :status=%s hpack_ok=%d end=%s frames=[%s]",
				st.http_status[0] ? st.http_status : "?",
				st.hpack_ok, h2_end_reason_name(st.end_reason), frames);
		}
		string_free(st.data);
		return 0;
	}

	if (!st.stream_ended) {
		if (status_msg && status_msg_size) {
			h2_summarize_frames((const uint8_t *)buf, len, frames, sizeof(frames));
			snprintf(status_msg, status_msg_size,
				"incomplete unary (no END_STREAM) grpc-status=%s :status=%s hpack_ok=%d end=%s goaway=%d rst=%d frames=[%s]",
				st.have_status ? "seen" : "missing",
				st.http_status[0] ? st.http_status : "?",
				st.hpack_ok, h2_end_reason_name(st.end_reason),
				st.saw_goaway, st.saw_rst, frames);
		}
		string_free(st.data);
		return 0;
	}

	/* Peer rejected the connection/stream before a unary gRPC status. */
	if (st.end_reason == H2_END_GOAWAY || st.end_reason == H2_END_RST) {
		h2_summarize_frames((const uint8_t *)buf, len, frames, sizeof(frames));
		if (status_msg && status_msg_size) {
			if (st.end_reason == H2_END_GOAWAY)
				snprintf(status_msg, status_msg_size,
					"peer GOAWAY before grpc-status error=%u(%s) last_stream=%u :status=%s hpack_ok=%d frames=[%s]",
					st.goaway_error, h2_error_name(st.goaway_error),
					st.goaway_last_sid,
					st.http_status[0] ? st.http_status : "?",
					st.hpack_ok, frames);
			else
				snprintf(status_msg, status_msg_size,
					"peer RST_STREAM before grpc-status error=%u(%s) :status=%s hpack_ok=%d frames=[%s]",
					st.rst_error, h2_error_name(st.rst_error),
					st.http_status[0] ? st.http_status : "?",
					st.hpack_ok, frames);
		}
		if (grpc_status)
			*grpc_status = -1;
		string_free(st.data);
		/* Return 1 so the catcher can print status_msg; grpc_status stays -1. */
		return 1;
	}

	if (grpc_status)
		*grpc_status = st.have_status ? st.grpc_status : -1;
	if (status_msg && status_msg_size) {
		h2_summarize_frames((const uint8_t *)buf, len, frames, sizeof(frames));
		if (!st.have_status) {
			snprintf(status_msg, status_msg_size,
				"missing grpc-status :status=%s hpack_ok=%d end=%s frames=[%s]",
				st.http_status[0] ? st.http_status : "?",
				st.hpack_ok, h2_end_reason_name(st.end_reason), frames);
		} else if (st.status_msg[0]) {
			/* grpc-message trailer present — keep it, append diagnostics. */
			snprintf(status_msg, status_msg_size,
				"grpc-status=%d :status=%s hpack_ok=%d end=%s grpc-message=\"%s\" frames=[%s]",
				st.grpc_status,
				st.http_status[0] ? st.http_status : "?",
				st.hpack_ok, h2_end_reason_name(st.end_reason),
				st.status_msg, frames);
		} else {
			snprintf(status_msg, status_msg_size,
				"grpc-status=%d :status=%s hpack_ok=%d end=%s frames=[%s]",
				st.grpc_status,
				st.http_status[0] ? st.http_status : "?",
				st.hpack_ok, h2_end_reason_name(st.end_reason), frames);
		}
	}

	if (st.data->l == 0) {
		string_free(st.data);
		return 1;
	}

	msg = otlp_grpc_frame_unwrap(st.data->s, st.data->l, &msg_len);
	if (!msg) {
		if (status_msg && status_msg_size) {
			h2_summarize_frames((const uint8_t *)buf, len, frames, sizeof(frames));
			snprintf(status_msg, status_msg_size,
				"gRPC data frame unwrap failed :status=%s hpack_ok=%d end=%s frames=[%s]",
				st.http_status[0] ? st.http_status : "?",
				st.hpack_ok, h2_end_reason_name(st.end_reason), frames);
		}
		string_free(st.data);
		return 0;
	}
	if (protobuf_out) {
		*protobuf_out = malloc(msg_len ? msg_len : 1);
		if (!*protobuf_out) {
			string_free(st.data);
			return 0;
		}
		if (msg_len)
			memcpy(*protobuf_out, msg, msg_len);
		else
			(*protobuf_out)[0] = '\0';
	}
	if (protobuf_len)
		*protobuf_len = msg_len;
	string_free(st.data);
	return 1;
}
