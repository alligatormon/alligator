#pragma once
uint64_t chunk_calc(context_arg* carg, char *buf, ssize_t nread, uint8_t copying);
int8_t tcp_check_full(context_arg* carg, char *buf, size_t nread, uint64_t chunksize);
