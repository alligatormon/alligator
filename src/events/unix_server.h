#pragma once
void unix_server_stop(const char* file);
void unix_server_init(uv_loop_t *loop, const char* file, context_arg *carg);
