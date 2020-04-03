#pragma once
#include "common/rtime.h"
#include "uv_tls.h"

void tls_client_handler();
void do_tls_client(char *addr, char *port, void *handler, char *mesg, int proto, void *data);
void do_tls_client_buffer(char *addr, char *port, void *handler, uv_buf_t* buffer, size_t buflen, int proto, void *data);
void tls_on_read(uv_tls_t* tcp, ssize_t nread, const uv_buf_t *buf);
void tls_on_write(uv_write_t* req, int status);
