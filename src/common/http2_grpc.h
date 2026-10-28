#pragma once

#include <stddef.h>
#include <stdint.h>
#include "common/selector.h"
#include "dstructures/tommy.h"
#include "dstructures/ht.h"

struct context_arg;

/* HTTP/2 gRPC client (no nghttp2): prior-knowledge preface, HPACK decoder
 * with a small dynamic table, SETTINGS/PING ACK, unary MetricsService/Export,
 * and send/receive flow-control (WINDOW_UPDATE + SETTINGS_INITIAL_WINDOW_SIZE).
 *
 * Remaining MVP limits: metrics only; no gzip; oneshot per export (no pooling);
 * no CONTINUATION on the request; decoder dtable capped (oversized incremental
 * entries skipped); cleartext is prior-knowledge only (no h2c upgrade). */

#define OTLP_GRPC_METRICS_PATH "/opentelemetry.proto.collector.metrics.v1.MetricsService/Export"
#define H2_DEFAULT_WINDOW_SIZE 65535u
#define H2_DEFAULT_MAX_FRAME_SIZE 16384u

/* Wrap ExportMetricsServiceRequest protobuf as a gRPC data frame
 * (1 byte compress=0 + 4-byte BE length + body). Caller frees with string_free. */
string *otlp_grpc_frame_wrap(const char *protobuf, size_t protobuf_len);

/* Unwrap a single gRPC data frame. Returns pointer into *frame and length via
 * out_len, or NULL on error. Compressed frames (flag != 0) are rejected. */
const char *otlp_grpc_frame_unwrap(const char *frame, size_t frame_len, size_t *out_len);

/* Build a oneshot HTTP/2 prior-knowledge request: connection preface + SETTINGS
 * + HEADERS + DATA within the default send window (65535). path may be NULL/"/"
 * to use OTLP_GRPC_METRICS_PATH. scheme_https selects :scheme. Optional env
 * supplies extra request headers (Authorization, …).
 *
 * If the framed body exceeds the default window, only the first window is
 * included (without END_STREAM) and the remainder is returned via *pending_out
 * (malloc'd; caller must attach with otlp_grpc_http2_flow_attach or free).
 * If pending_out is NULL and the body does not fit, returns NULL.
 * Returns malloc'd wire buffer; *out_len set. Caller frees. */
char *otlp_grpc_http2_request_build(const char *authority, const char *path,
	int scheme_https, const char *grpc_framed, size_t grpc_framed_len,
	alligator_ht *env, size_t *out_len,
	char **pending_out, size_t *pending_len_out);

/* Transfer ownership of a pending DATA body onto carg and init send windows
 * after already_sent bytes were included in the initial wire buffer. */
void otlp_grpc_http2_flow_attach(struct context_arg *carg,
	char *pending, size_t pending_len, size_t already_sent);

/* expect_function for oneshot: ACK peer SETTINGS/PING, apply WINDOW_UPDATE /
 * SETTINGS to send windows, flush pending DATA, return 1 when stream 1 has
 * ended (trailers END_STREAM) so the catcher can run without waiting EOF. */
int8_t otlp_grpc_expect_complete(struct context_arg *carg, char *buf, size_t nread);

/* Parse accumulated HTTP/2 response bytes. Sets *grpc_status (-1 if missing),
 * optionally copies *status_msg, and returns malloc'd unwrapped protobuf body
 * (may be NULL/0 for empty success). Caller frees *protobuf_out. Returns 0 on
 * hard parse failure, 1 when a complete unary response was understood. */
int otlp_grpc_http2_response_parse(const char *buf, size_t len,
	int *grpc_status, char *status_msg, size_t status_msg_size,
	char **protobuf_out, size_t *protobuf_len);
