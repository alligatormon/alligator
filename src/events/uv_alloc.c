#include <uv.h>
#include <stdlib.h>
#include "events/context_arg.h"
//void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
//{
//	*buf = uv_buf_init((char*)malloc(suggested_size), suggested_size);
//	printf("allocated %p with size %zu\n", *buf =
//}

void alloc_buffer(uv_handle_t* handle, size_t size, uv_buf_t* buf)
{
	//(void)handle;
	context_arg *carg = handle->data;
	if (carg && carg->uvbuf)
	{
		buf->base = carg->uvbuf;
		bzero(buf->base, size);
	}
	else
	{
		buf->base = calloc(1, size);
	}
	buf->len = size;
}

void tcp_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
	context_arg* carg = (context_arg*)handle->data;
	*buf = uv_buf_init(carg->net_buffer_in, EVENT_BUFFER);
}
