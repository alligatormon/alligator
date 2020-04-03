#pragma once
#include "common/rtime.h"

void tcp_client_handler();
//void do_tcp_client(char *addr, char *port, void *handler, char *mesg, int proto, void *data);
void do_tcp_client(char *addr, char *port, void *handler, char *mesg, int proto, void *data, int tls);
//void do_tcp_client_buffer(char *addr, char *port, void *handler, uv_buf_t* buffer, size_t buflen, int proto, void *data);
void do_tcp_client_buffer(char *addr, char *port, void *handler, uv_buf_t* buffer, size_t buflen, int proto, void *data, int tls);
void tcp_on_read(uv_stream_t* tcp, ssize_t nread, const uv_buf_t *buf);
void on_write(uv_write_t* req, int status);
