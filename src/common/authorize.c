#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include "common/authorize.h"

int authorize_compute(const char *secret, uint64_t bucket, char out_hex[AUTHORIZE_HEX_LEN + 1])
{
	unsigned char mac[EVP_MAX_MD_SIZE];
	unsigned int mac_len = 0;
	char bucket_dec[32];
	int bucket_len;
	size_t slen;
	size_t mlen;
	unsigned char *msg;
	unsigned char *rc;
	static const char hexd[] = "0123456789abcdef";
	unsigned int i;

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

	rc = HMAC(EVP_sha256(), secret, (int)slen, msg, mlen, mac, &mac_len);
	OPENSSL_cleanse(msg, mlen);
	free(msg);
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

int8_t authorize_validate(const char *secret, uint64_t period, const char *presented, int64_t now)
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

		if (!authorize_compute(secret, b, expect))
			return 0;

		eq = (CRYPTO_memcmp(presented, expect, AUTHORIZE_HEX_LEN) == 0);
		OPENSSL_cleanse(expect, sizeof(expect));
		if (valid && eq)
			ok = 1;
	}

	return ok ? 1 : 0;
}

int http_uri_is_authorize(const char *uri)
{
	if (!uri)
		return 0;
	if (strncmp(uri, "/authorize", 10))
		return 0;
	return uri[10] == '\0' || uri[10] == '?';
}
