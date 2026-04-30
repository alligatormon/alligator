#pragma once
int64_t chunk_calc(context_arg* carg, char *buf, ssize_t nread, uint8_t copying, uint64_t *decoded_size);
int8_t tcp_check_full(context_arg* carg, char *buf, size_t nread, int64_t chunk_ret);
