#pragma once
#include <stdint.h>
#include <stddef.h>
#include <jansson.h>

struct sockaddr;
struct context_arg;
struct http_reply_data;

/* Unbound message: master_secret || ":" || decimal_bucket.
   Bound message: "v1" plus a decimal length prefix on the bucket and on each
   bind label and value. The HMAC key is always the master secret.
   Lowercase hex, 32 bytes → 64 characters. */
#define AUTHORIZE_HEX_LEN 64
#define AUTHORIZE_BIND_MAX 8
#define AUTHORIZE_PEER_SIZE 64

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

enum {
	AUTHORIZE_BIND_REMOTE_ADDR = 1,
	AUTHORIZE_BIND_HEADER = 2,
	AUTHORIZE_BIND_COOKIE = 3
};

enum {
	AUTHORIZE_FACTORS_PEER = -1,
	AUTHORIZE_FACTORS_MISSING = 0,
	AUTHORIZE_FACTORS_OK = 1
};

typedef struct authorize_bind {
	uint8_t kind;
	char *name;
} authorize_bind;

/* Writes AUTHORIZE_HEX_LEN hex chars and a NUL. Returns 1 on success.
   No binds: legacy message. */
int authorize_compute(const char *secret, uint64_t bucket, char out_hex[AUTHORIZE_HEX_LEN + 1]);

/* values[i] is the request value for binds[i]. Empty or NULL fails. nbinds == 0 uses authorize_compute. */
int authorize_compute_bound(const char *secret, uint64_t bucket, const authorize_bind *binds, uint8_t nbinds, const char *const *values, char out_hex[AUTHORIZE_HEX_LEN + 1]);

/* 1 match (bucket-1, bucket, bucket+1), 0 present but wrong, -1 missing or not configured. */
int8_t authorize_validate(const char *secret, uint64_t period, const char *presented, int64_t now);

int8_t authorize_validate_bound(const char *secret, uint64_t period, const char *presented, int64_t now, const authorize_bind *binds, uint8_t nbinds, const char *const *values);

/* URI is exactly "/authorize" or "/authorize" followed by '?'. */
int http_uri_is_authorize(const char *uri);

/* Canonical peer without a port. IPv4-mapped IPv6 is written as IPv4. Returns 1 on success. */
int authorize_format_peer(const struct sockaddr *sa, char *out, size_t outlen);

/* Read the accepted TCP peer into carg->authorize_peer. Clears the field on failure. */
void authorize_peer_capture(struct context_arg *carg);

/* Append one bind. name is required for header and cookie, and must not be the code header or cookie.
   remote_addr takes no name. Returns 1 on success. */
int authorize_bind_add(struct context_arg *carg, int kind, const char *name);

/* JSON array of {"type":"remote_addr"|"header"|"cookie","name":"..."}.
   Returns the number added. A rejected entry frees every bind, sets
   authorize_config_error, and returns -1. */
int authorize_binds_apply_json(struct context_arg *carg, json_t *jbind);

authorize_bind *authorize_binds_dup(const authorize_bind *src, uint8_t n);
void authorize_binds_free(authorize_bind *binds, uint8_t n);

/* "remote_addr", "header:<name>", or "cookie:<name>". Returns the length, or -1. */
int authorize_bind_label(const authorize_bind *bind, char *out, size_t outlen);

/* Fill malloc'd values parallel to carg->authorize_binds.
   AUTHORIZE_FACTORS_OK, AUTHORIZE_FACTORS_MISSING (header/cookie), or AUTHORIZE_FACTORS_PEER.
   On failure every values[i] is NULL. fail_index receives the failing bind when non-NULL. */
int authorize_factors_fill(struct context_arg *carg, struct http_reply_data *http, char **values, uint8_t *fail_index);

void authorize_factors_free(char **values, uint8_t n);
