#pragma once

#include <stddef.h>
#include "events/context_arg.h"
#include "common/url.h"
#include "dstructures/ht.h"

/* host:port, or [host]:port when host itself contains ':'. */
void probe_grpc_authority(const char *host, const char *port, char *dst, size_t dst_len);

/* On-demand target → tcp:// or tls:// URL plus host:port authority.
 * Returns 0 on success. A leading http:// or https:// is stripped and does
 * not select TLS; tls comes from the caller. */
int probe_grpc_format_target(int tls, const char *target, char *url, size_t url_len, char *authority, size_t authority_len);

/* Prior-knowledge HTTP/2 Health/Check request. Caller frees. *out_len is the
 * byte length (the buffer contains NUL bytes). */
char *probe_grpc_build_request(const char *authority, const char *service, int scheme_https, alligator_ht *env, size_t *out_len);

/* After context_arg_json_fill for an aggregate grpc:// or grpcs:// URL. */
void probe_grpc_arm_carg(context_arg *carg, host_aggregator_info *hi, const char *service, alligator_ht *env);

/* 1 when a complete unary response was understood.
 * *grpc_status is the parsed code, or -1 when the parser left it unset.
 * *health_status is 0..3 when grpc_status is 0 (missing field → UNKNOWN),
 * otherwise -1. */
int probe_grpc_interpret(const char *buf, size_t len, int *grpc_status, int *health_status);

void grpc_health_probe_catch(char *metrics, size_t size, context_arg *carg);

/* Connect, TLS, or timeout before any grpc-status: code 14, probe_success 0. */
void probe_grpc_fail_unavailable(context_arg *carg);

int probe_grpc_is(context_arg *carg);
