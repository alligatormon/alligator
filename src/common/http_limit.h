#pragma once
#include <stdint.h>
#include <jansson.h>

struct context_arg;
struct http_reply_data;
struct http_limit;

/* Install rules from a JSON array. A non-empty array that does not install every
   entry, or an allocation failure, sets carg->http_limit_config_error and installs
   nothing. Returns the number installed, or -1 if jlimit is not an array (or carg
   is NULL). A NULL jlimit or an empty array installs nothing and returns 0. */
int http_limit_apply_json(struct context_arg *carg, json_t *jlimit);

/* 1 allow, including when no rule matches. 0 deny.
   On deny, *retry_after_sec is at least 1 when the pointer is non-NULL.
   now_ns is a monotonic clock (uv_hrtime in the parser). */
int http_limit_check(struct context_arg *carg, struct http_reply_data *http, uint64_t now_ns, uint64_t *retry_after_sec);

void http_limit_incref(struct http_limit *lim);
void http_limit_decref(struct http_limit *lim);
