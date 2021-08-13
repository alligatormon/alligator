#include <uv.h>
#include <stdlib.h>
#include "events/context_arg.h"

void free_buffer(context_arg *carg, uv_buf_t* buf)
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
	context_arg *carg = handle->data;
	if (carg && carg->uvbuf)
	{
		buf->base = carg->uvbuf;
		bzero(buf->base, size);
		//printf("use carg bufbase %p: %zu\n", buf->base, size);
	}
	else
	{
		buf->base = calloc(1, size);
		//printf("alloc bufbase %p: %zu\n", buf->base, size);
	}
	buf->len = size;
}

void tcp_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
	context_arg* carg = (context_arg*)handle->data;
	*buf = uv_buf_init(carg->net_buffer_in, EVENT_BUFFER);
}
