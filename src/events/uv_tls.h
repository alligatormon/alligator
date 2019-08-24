#include <stdio.h>
#include <uv.h>
#include <assert.h>
#include "evt_tls.h"

typedef struct uv_tls_s uv_tls_t;

typedef void (*uv_handshake_cb)(uv_tls_t*, int);
typedef void (*uv_tls_write_cb)(uv_tls_t*, int);
typedef void (*uv_tls_read_cb)(uv_tls_t*, ssize_t, const uv_buf_t*);
typedef void (*uv_tls_close_cb)(uv_tls_t*);

struct uv_tls_s {
   uv_tcp_t *tcp_hdl;
   evt_tls_t *tls;
   void *data;

   uv_tls_read_cb tls_rd_cb;
   uv_tls_close_cb tls_cls_cb;
   uv_handshake_cb tls_hsk_cb;
   uv_tls_write_cb tls_wr_cb;
};

int uv_tls_writer(evt_tls_t *t, void *bfr, int sz);
int uv_tls_init(evt_ctx_t *ctx, uv_tcp_t *tcp, uv_tls_t *endpt);
int uv_tls_connect(uv_tls_t *t, uv_handshake_cb cb);
int uv_tls_accept(uv_tls_t *tls, uv_handshake_cb cb);
int uv_tls_read(uv_tls_t *tls, uv_tls_read_cb on_read);
int uv_tls_close(uv_tls_t* session, uv_tls_close_cb close_cb);


int uv_tls_write(uv_tls_t *stream, uv_buf_t* buf, uv_tls_write_cb cb);

