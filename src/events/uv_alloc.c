#include <uv.h>
#include <stdlib.h>
#include "events/context_arg.h"

void free_buffer(context_arg *carg, const uv_buf_t* buf)
{
	if (carg && carg->uvbuf && buf->base == carg->uvbuf)
	{
		//printf("\twont free!! %p\n", buf->base);
	}
	else
	{
		//printf("\tfree bufbase %p\n", buf->base);
		free(buf->base);
	}
}

void alloc_buffer(uv_handle_t* handle, size_t size, uv_buf_t* buf)
{
	/* handle->data is not always a context_arg (shared DNS UDP binds store
	 * resolver_udp_bind*). Never follow carg->uvbuf here: a wrong pointer
	 * plus bzero(64KiB) zeros uv_udp_t.recv_cb and libuv jumps to NULL. */
	(void)handle;
	buf->base = calloc(1, size);
	buf->len = buf->base ? size : 0;
}

void tcp_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
	context_arg* carg = (context_arg*)handle->data;
	*buf = uv_buf_init(carg->net_buffer_in, EVENT_BUFFER);
}
