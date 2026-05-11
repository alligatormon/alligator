#pragma once
#include "events/context_arg.h"

/* Start a persistent WebSocket client for the given carg.
   carg->host, carg->numport, carg->query_url, carg->parser_handler
   and carg->period must be set before calling.
   Returns a non-NULL type string on success. */
char *ws_client(context_arg *carg);

/* Tear down an active ws_client connection. */
void ws_client_del(context_arg *carg);
