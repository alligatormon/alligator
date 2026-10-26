#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <uv.h>
#include "events/context_arg.h"
#include "parsers/http_proto.h"
#include "common/authorize.h"
#include "common/logs.h"

#define AUTHORIZE_VALUE_MAX 4096
#define AUTHORIZE_MSG_MAX (64 * 1024)

static int authorize_hmac_hex(const char *secret, size_t slen, const unsigned char *msg, size_t mlen, char out_hex[AUTHORIZE_HEX_LEN + 1])
{
	unsigned char mac[EVP_MAX_MD_SIZE];
	unsigned int mac_len = 0;
	unsigned char *rc;
	static const char hexd[] = "0123456789abcdef";
	unsigned int i;

	rc = HMAC(EVP_sha256(), secret, (int)slen, msg, mlen, mac, &mac_len);
	if (!rc || mac_len != 32)
	{
		OPENSSL_cleanse(mac, sizeof(mac));
		return 0;
	}

	for (i = 0; i < mac_len; i++)
	{
		out_hex[i * 2] = hexd[mac[i] >> 4];
		out_hex[i * 2 + 1] = hexd[mac[i] & 0x0f];
	}
	out_hex[AUTHORIZE_HEX_LEN] = 0;
	OPENSSL_cleanse(mac, sizeof(mac));
	return 1;
}

int authorize_compute(const char *secret, uint64_t bucket, char out_hex[AUTHORIZE_HEX_LEN + 1])
{
	char bucket_dec[32];
	int bucket_len;
	size_t slen;
	size_t mlen;
	unsigned char *msg;
	int ok;

	if (!secret || !secret[0] || !out_hex)
		return 0;

	slen = strlen(secret);
	if (slen > 1024 * 1024)
		return 0;

	bucket_len = snprintf(bucket_dec, sizeof(bucket_dec), "%" PRIu64, bucket);
	if (bucket_len <= 0 || (size_t)bucket_len >= sizeof(bucket_dec))
		return 0;

	mlen = slen + 1 + (size_t)bucket_len;
	msg = malloc(mlen);
	if (!msg)
		return 0;

	memcpy(msg, secret, slen);
	msg[slen] = ':';
	memcpy(msg + slen + 1, bucket_dec, (size_t)bucket_len);

	ok = authorize_hmac_hex(secret, slen, msg, mlen, out_hex);
	OPENSSL_cleanse(msg, mlen);
	free(msg);
	return ok;
}

static int authorize_msg_add(unsigned char **buf, size_t *len, size_t *cap, const void *p, size_t n)
{
	size_t ncap;
	unsigned char *nb;

	if (!n)
		return 1;
	if (*len + n < *len || *len + n > AUTHORIZE_MSG_MAX)
		return 0;
	if (*len + n > *cap)
	{
		ncap = *cap ? *cap : 128;
		while (ncap < *len + n)
		{
			if (ncap > AUTHORIZE_MSG_MAX / 2)
			{
				ncap = AUTHORIZE_MSG_MAX;
				break;
			}
			ncap *= 2;
		}
		if (ncap < *len + n)
			return 0;
		nb = realloc(*buf, ncap);
		if (!nb)
			return 0;
		*buf = nb;
		*cap = ncap;
	}
	memcpy(*buf + *len, p, n);
	*len += n;
	return 1;
}

static int authorize_msg_add_lp(unsigned char **buf, size_t *len, size_t *cap, const char *s, size_t n)
{
	char dec[32];
	int dlen;

	if (n > AUTHORIZE_VALUE_MAX)
		return 0;
	dlen = snprintf(dec, sizeof(dec), "%zu:", n);
	if (dlen <= 0 || (size_t)dlen >= sizeof(dec))
		return 0;
	if (!authorize_msg_add(buf, len, cap, dec, (size_t)dlen))
		return 0;
	return authorize_msg_add(buf, len, cap, s, n);
}

int authorize_bind_label(const authorize_bind *bind, char *out, size_t outlen)
{
	int n;

	if (!bind || !out || !outlen)
		return -1;
	if (bind->kind == AUTHORIZE_BIND_REMOTE_ADDR)
		n = snprintf(out, outlen, "remote_addr");
	else if (bind->kind == AUTHORIZE_BIND_HEADER)
		n = snprintf(out, outlen, "header:%s", bind->name ? bind->name : "");
	else if (bind->kind == AUTHORIZE_BIND_COOKIE)
		n = snprintf(out, outlen, "cookie:%s", bind->name ? bind->name : "");
	else
		return -1;
	if (n <= 0 || (size_t)n >= outlen)
		return -1;
	return n;
}

int authorize_compute_bound(const char *secret, uint64_t bucket, const authorize_bind *binds, uint8_t nbinds, const char *const *values, char out_hex[AUTHORIZE_HEX_LEN + 1])
{
	char bucket_dec[32];
	int bucket_len;
	size_t slen;
	unsigned char *msg = NULL;
	size_t mlen = 0;
	size_t mcap = 0;
	uint8_t i;
	int ok = 0;

	if (!nbinds)
		return authorize_compute(secret, bucket, out_hex);
	if (!secret || !secret[0] || !out_hex || !binds || !values)
		return 0;

	slen = strlen(secret);
	if (slen > 1024 * 1024)
		return 0;

	bucket_len = snprintf(bucket_dec, sizeof(bucket_dec), "%" PRIu64, bucket);
	if (bucket_len <= 0 || (size_t)bucket_len >= sizeof(bucket_dec))
		return 0;

	if (!authorize_msg_add(&msg, &mlen, &mcap, "v1", 2))
		goto out;
	if (!authorize_msg_add_lp(&msg, &mlen, &mcap, bucket_dec, (size_t)bucket_len))
		goto out;

	for (i = 0; i < nbinds; i++)
	{
		char label[160];
		int llen;
		size_t vlen;

		if ((binds[i].kind == AUTHORIZE_BIND_HEADER || binds[i].kind == AUTHORIZE_BIND_COOKIE) &&
		    (!binds[i].name || !binds[i].name[0]))
			goto out;
		if (!values[i] || !values[i][0])
			goto out;
		vlen = strlen(values[i]);
		llen = authorize_bind_label(&binds[i], label, sizeof(label));
		if (llen < 0)
			goto out;
		if (!authorize_msg_add_lp(&msg, &mlen, &mcap, label, (size_t)llen))
			goto out;
		if (!authorize_msg_add_lp(&msg, &mlen, &mcap, values[i], vlen))
			goto out;
	}

	ok = authorize_hmac_hex(secret, slen, msg, mlen, out_hex);
out:
	if (msg)
	{
		OPENSSL_cleanse(msg, mlen);
		free(msg);
	}
	return ok;
}

int8_t authorize_validate_bound(const char *secret, uint64_t period, const char *presented, int64_t now, const authorize_bind *binds, uint8_t nbinds, const char *const *values)
{
	uint64_t bucket;
	int delta;
	int ok = 0;

	if (!secret || !secret[0] || !period)
		return -1;
	if (!presented || !presented[0])
		return -1;
	if (strlen(presented) != AUTHORIZE_HEX_LEN)
		return 0;
	if (now < 0)
		return 0;
	if (nbinds && (!binds || !values))
		return 0;

	bucket = (uint64_t)now / period;
	for (delta = -1; delta <= 1; delta++)
	{
		char expect[AUTHORIZE_HEX_LEN + 1];
		uint64_t b;
		int valid = 1;
		int eq;

		if (delta < 0)
		{
			if (!bucket)
				valid = 0;
			b = bucket ? bucket - 1 : 0;
		}
		else if (delta > 0)
			b = bucket + 1;
		else
			b = bucket;

		if (!authorize_compute_bound(secret, b, binds, nbinds, values, expect))
			return 0;

		eq = (CRYPTO_memcmp(presented, expect, AUTHORIZE_HEX_LEN) == 0);
		OPENSSL_cleanse(expect, sizeof(expect));
		if (valid && eq)
			ok = 1;
	}

	return ok ? 1 : 0;
}

int8_t authorize_validate(const char *secret, uint64_t period, const char *presented, int64_t now)
{
	return authorize_validate_bound(secret, period, presented, now, NULL, 0, NULL);
}

int http_uri_is_authorize(const char *uri)
{
	if (!uri)
		return 0;
	if (strncmp(uri, "/authorize", 10))
		return 0;
	return uri[10] == '\0' || uri[10] == '?';
}

static int authorize_v4mapped(const struct in6_addr *a)
{
	static const unsigned char pfx[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff };

	return memcmp(a->s6_addr, pfx, 12) == 0;
}

int authorize_format_peer(const struct sockaddr *sa, char *out, size_t outlen)
{
	char *zone;

	if (!sa || !out || outlen < 2)
		return 0;
	out[0] = 0;
	if (sa->sa_family == AF_INET)
	{
		if (uv_ip4_name((const struct sockaddr_in *)sa, out, outlen) != 0)
			return 0;
		return out[0] != 0;
	}
	if (sa->sa_family == AF_INET6)
	{
		const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)sa;

		if (authorize_v4mapped(&in6->sin6_addr))
		{
			struct sockaddr_in in4;

			memset(&in4, 0, sizeof(in4));
			in4.sin_family = AF_INET;
			memcpy(&in4.sin_addr, in6->sin6_addr.s6_addr + 12, 4);
			if (uv_ip4_name(&in4, out, outlen) != 0)
				return 0;
			return out[0] != 0;
		}
		if (uv_ip6_name(in6, out, outlen) != 0)
			return 0;
		zone = strchr(out, '%');
		if (zone)
			*zone = 0;
		return out[0] != 0;
	}
	return 0;
}

void authorize_peer_capture(context_arg *carg)
{
	struct sockaddr_storage ss;
	int slen = (int)sizeof(ss);
	char tmp[AUTHORIZE_PEER_SIZE];

	if (!carg)
		return;
	carg->authorize_peer[0] = 0;
	if (uv_tcp_getpeername(&carg->client, (struct sockaddr *)&ss, &slen) != 0)
		return;
	if (!authorize_format_peer((struct sockaddr *)&ss, tmp, sizeof(tmp)))
		return;
	strlcpy(carg->authorize_peer, tmp, sizeof(carg->authorize_peer));
}

static int authorize_bind_name_ok(int kind, const char *name)
{
	const char *p;

	if (!name || !name[0] || strlen(name) > 128)
		return 0;
	for (p = name; *p; p++)
	{
		unsigned char c = (unsigned char)*p;

		if (c <= 32 || c >= 127)
			return 0;
		if (kind == AUTHORIZE_BIND_COOKIE && (c == ';' || c == '=' || c == ','))
			return 0;
		if (kind == AUTHORIZE_BIND_HEADER && c == ':')
			return 0;
	}
	return 1;
}

int authorize_bind_add(context_arg *carg, int kind, const char *name)
{
	authorize_bind *n;
	char *stored = NULL;

	if (!carg)
		return 0;
	if (carg->authorize_bind_count >= AUTHORIZE_BIND_MAX)
	{
		glog(L_ERROR, "entrypoint: authorize bind limit %d exceeded\n", AUTHORIZE_BIND_MAX);
		return 0;
	}
	if (kind == AUTHORIZE_BIND_REMOTE_ADDR)
	{
		if (name && name[0])
		{
			glog(L_ERROR, "entrypoint: authorize bind remote_addr does not take a name\n");
			return 0;
		}
	}
	else if (kind == AUTHORIZE_BIND_HEADER || kind == AUTHORIZE_BIND_COOKIE)
	{
		if (!authorize_bind_name_ok(kind, name))
		{
			glog(L_ERROR, "entrypoint: authorize bind name rejected\n");
			return 0;
		}
		if (kind == AUTHORIZE_BIND_HEADER && carg->authorize_header_name &&
		    !strcasecmp(name, carg->authorize_header_name))
		{
			glog(L_ERROR, "entrypoint: authorize bind header '%s' is the code header\n", name);
			return 0;
		}
		if (kind == AUTHORIZE_BIND_COOKIE && carg->authorize_cookie_name &&
		    !strcmp(name, carg->authorize_cookie_name))
		{
			glog(L_ERROR, "entrypoint: authorize bind cookie '%s' is the code cookie\n", name);
			return 0;
		}
		stored = strdup(name);
		if (!stored)
			return 0;
	}
	else
	{
		glog(L_ERROR, "entrypoint: unknown authorize bind type\n");
		return 0;
	}

	n = realloc(carg->authorize_binds, (carg->authorize_bind_count + 1) * sizeof(*n));
	if (!n)
	{
		free(stored);
		return 0;
	}
	carg->authorize_binds = n;
	n[carg->authorize_bind_count].kind = (uint8_t)kind;
	n[carg->authorize_bind_count].name = stored;
	carg->authorize_bind_count++;
	return 1;
}

static void authorize_binds_reject(context_arg *carg)
{
	if (!carg)
		return;
	authorize_binds_free(carg->authorize_binds, carg->authorize_bind_count);
	carg->authorize_binds = NULL;
	carg->authorize_bind_count = 0;
	carg->authorize_config_error = 1;
	glog(L_ERROR, "entrypoint: authorize bind rejected; requests will be denied\n");
}

int authorize_binds_apply_json(context_arg *carg, json_t *jbind)
{
	size_t i;
	size_t n;
	int added = 0;

	if (!carg)
		return -1;
	if (!jbind)
		return 0;
	if (!json_is_array(jbind))
	{
		glog(L_ERROR, "entrypoint: authorize bind must be an array\n");
		authorize_binds_reject(carg);
		return -1;
	}

	n = json_array_size(jbind);
	for (i = 0; i < n; i++)
	{
		json_t *one = json_array_get(jbind, i);
		const char *type;
		const char *bname;
		int kind = 0;
		int ok = 0;

		if (!json_is_object(one))
			glog(L_ERROR, "entrypoint: authorize bind must be an object\n");
		else
		{
			type = json_string_value(json_object_get(one, "type"));
			bname = json_string_value(json_object_get(one, "name"));
			if (!type)
				glog(L_ERROR, "entrypoint: authorize bind type is missing\n");
			else if (!strcmp(type, "remote_addr"))
				kind = AUTHORIZE_BIND_REMOTE_ADDR;
			else if (!strcmp(type, "header"))
				kind = AUTHORIZE_BIND_HEADER;
			else if (!strcmp(type, "cookie"))
				kind = AUTHORIZE_BIND_COOKIE;
			else
				glog(L_ERROR, "entrypoint: unknown authorize bind '%s'\n", type);
			if (kind)
				ok = authorize_bind_add(carg, kind, bname);
		}
		if (!ok)
		{
			authorize_binds_reject(carg);
			return -1;
		}
		added++;
	}
	return added;
}

authorize_bind *authorize_binds_dup(const authorize_bind *src, uint8_t n)
{
	authorize_bind *dst;
	uint8_t i;

	if (!n || !src)
		return NULL;
	dst = calloc(n, sizeof(*dst));
	if (!dst)
		return NULL;
	for (i = 0; i < n; i++)
	{
		dst[i].kind = src[i].kind;
		if (src[i].name)
		{
			dst[i].name = strdup(src[i].name);
			if (!dst[i].name)
			{
				authorize_binds_free(dst, n);
				return NULL;
			}
		}
	}
	return dst;
}

void authorize_binds_free(authorize_bind *binds, uint8_t n)
{
	uint8_t i;

	if (!binds)
		return;
	for (i = 0; i < n; i++)
		free(binds[i].name);
	free(binds);
}

void authorize_factors_free(char **values, uint8_t n)
{
	uint8_t i;

	if (!values)
		return;
	for (i = 0; i < n; i++)
	{
		if (!values[i])
			continue;
		OPENSSL_cleanse(values[i], strlen(values[i]));
		free(values[i]);
		values[i] = NULL;
	}
}

int authorize_factors_fill(context_arg *carg, http_reply_data *http, char **values, uint8_t *fail_index)
{
	uint8_t i;

	if (!carg || !values)
		return AUTHORIZE_FACTORS_MISSING;
	if (!carg->authorize_bind_count)
		return AUTHORIZE_FACTORS_OK;
	if (carg->authorize_bind_count > AUTHORIZE_BIND_MAX || !carg->authorize_binds)
		return AUTHORIZE_FACTORS_MISSING;

	memset(values, 0, sizeof(*values) * carg->authorize_bind_count);
	for (i = 0; i < carg->authorize_bind_count; i++)
	{
		authorize_bind *b = &carg->authorize_binds[i];
		char *v = NULL;

		if (b->kind == AUTHORIZE_BIND_REMOTE_ADDR)
		{
			if (!carg->authorize_peer[0])
			{
				if (fail_index)
					*fail_index = i;
				authorize_factors_free(values, carg->authorize_bind_count);
				return AUTHORIZE_FACTORS_PEER;
			}
			v = strdup(carg->authorize_peer);
		}
		else if (b->kind == AUTHORIZE_BIND_HEADER)
			v = http_header_get(http, b->name);
		else if (b->kind == AUTHORIZE_BIND_COOKIE)
			v = http_cookie_get(http, b->name);
		else
		{
			if (fail_index)
				*fail_index = i;
			authorize_factors_free(values, carg->authorize_bind_count);
			return AUTHORIZE_FACTORS_MISSING;
		}

		if (!v || !v[0] || strlen(v) > AUTHORIZE_VALUE_MAX)
		{
			free(v);
			if (fail_index)
				*fail_index = i;
			authorize_factors_free(values, carg->authorize_bind_count);
			return AUTHORIZE_FACTORS_MISSING;
		}
		values[i] = v;
	}
	return AUTHORIZE_FACTORS_OK;
}
