#pragma once
void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
void tcp_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
