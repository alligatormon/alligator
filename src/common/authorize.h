#pragma once
#include <stdint.h>
#include <stddef.h>

/* HMAC-SHA256(key = master_secret, message = master_secret || ":" || decimal_bucket).
   Lowercase hex, 32 bytes → 64 characters. */
#define AUTHORIZE_HEX_LEN 64

enum {
	AUTHORIZE_SET_COOKIE = 0,
	AUTHORIZE_SET_HEADER = 1,
	AUTHORIZE_SET_JSON = 2
};

enum {
	AUTHORIZE_VALIDATE_COOKIE = 0,
	AUTHORIZE_VALIDATE_HEADER = 1
};

enum {
	SATISFY_ANY = 0,
	SATISFY_ALL = 1
};

/* Writes AUTHORIZE_HEX_LEN hex chars and a NUL. Returns 1 on success. */
int authorize_compute(const char *secret, uint64_t bucket, char out_hex[AUTHORIZE_HEX_LEN + 1]);

/* 1 match (bucket-1, bucket, bucket+1), 0 present but wrong, -1 missing or not configured. */
int8_t authorize_validate(const char *secret, uint64_t period, const char *presented, int64_t now);

/* URI is exactly "/authorize" or "/authorize" followed by '?'. */
int http_uri_is_authorize(const char *uri);
