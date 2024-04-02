#pragma once
void unixgram_server_handler(char *addr, void* parser_handler);
void unixgram_client_handler();
void unixgram_server_stop(const char* addr);
void unixgram_server_init(uv_loop_t *loop, char *addr, context_arg *carg);
