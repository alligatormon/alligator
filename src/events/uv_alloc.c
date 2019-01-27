#include <uv.h>
#include <stdlib.h>
//void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
//{
//	*buf = uv_buf_init((char*)malloc(suggested_size), suggested_size);
//	printf("allocated %p with size %zu\n", *buf =
//}

void alloc_buffer(uv_handle_t* handle, size_t size, uv_buf_t* buf)
{
	(void)handle;
	buf->base = calloc(1, size);
	printf("allocated %p with size %zu\n", buf->base, size);
	buf->len = size;
}
