#pragma once
#include <stddef.h>
#include <stdint.h>
#include "events/context_arg.h"
#include "parsers/http_proto.h"

#define HTTP_ENTRYPOINT_IDLE_TIMEOUT_DEFAULT_SEC 75

void http_entrypoint_negotiate(context_arg *carg, const http_reply_data *hr);
size_t http_entrypoint_request_size(const char *buf, const http_reply_data *hr);
int http_entrypoint_request_complete(const char *buf, size_t buflen, const http_reply_data *hr, size_t *msg_size);
/* Returns buffer for uv_write; sets *allocated if caller must free after write. */
char *http_entrypoint_prepare_response(context_arg *carg, char *body, size_t len, int *allocated);
int http_entrypoint_should_shutdown(context_arg *carg, int write_status);
void http_entrypoint_reset_request_state(context_arg *carg);
void http_entrypoint_consume_request(context_arg *carg);
/* Append Content-Length: 0 and final CRLF for responses with no body (keep-alive safe). */
void http_entrypoint_finish_empty_body(string *response);
