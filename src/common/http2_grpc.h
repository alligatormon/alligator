#pragma once

#include <stddef.h>
#include <stdint.h>
#include "common/selector.h"
#include "dstructures/tommy.h"
#include "dstructures/ht.h"

struct context_arg;

/* MVP HTTP/2 gRPC client (no nghttp2): prior-knowledge preface, static HPACK,
 * SETTINGS/PING ACK, unary MetricsService/Export. No send flow-control
 * (payload ≤ 65535), no dynamic HPACK table, no gzip, oneshot per export. */

#define OTLP_GRPC_METRICS_PATH "/opentelemetry.proto.collector.metrics.v1.MetricsService/Export"

/* Wrap ExportMetricsServiceRequest protobuf as a gRPC data frame
 * (1 byte compress=0 + 4-byte BE length + body). Caller frees with string_free. */
string *otlp_grpc_frame_wrap(const char *protobuf, size_t protobuf_len);

/* Unwrap a single gRPC data frame. Returns pointer into *frame and length via
 * out_len, or NULL on error. Compressed frames (flag != 0) are rejected. */
const char *otlp_grpc_frame_unwrap(const char *frame, size_t frame_len, size_t *out_len);

/* Build a oneshot HTTP/2 prior-knowledge request: connection preface + SETTINGS
 * + HEADERS + DATA(END_STREAM) carrying the gRPC-framed protobuf body.
 * path may be NULL/"/" to use OTLP_GRPC_METRICS_PATH. scheme_https selects
 * :scheme. Optional env supplies extra request headers (Authorization, …).
 * Returns malloc'd buffer; *out_len set. Caller frees. */
char *otlp_grpc_http2_request_build(const char *authority, const char *path,
	int scheme_https, const char *grpc_framed, size_t grpc_framed_len,
	alligator_ht *env, size_t *out_len);

/* expect_function for oneshot: ACK peer SETTINGS once; return 1 when stream 1
 * has ended (trailers END_STREAM) so the catcher can run without waiting EOF. */
int8_t otlp_grpc_expect_complete(struct context_arg *carg, char *buf, size_t nread);

/* Parse accumulated HTTP/2 response bytes. Sets *grpc_status (-1 if missing),
 * optionally copies *status_msg, and returns malloc'd unwrapped protobuf body
 * (may be NULL/0 for empty success). Caller frees *protobuf_out. Returns 0 on
 * hard parse failure, 1 when a complete unary response was understood. */
int otlp_grpc_http2_response_parse(const char *buf, size_t len,
	int *grpc_status, char *status_msg, size_t status_msg_size,
	char **protobuf_out, size_t *protobuf_len);
