#pragma once
uv_buf_t* gen_fcgi_query(char *method_query, char *host, char *useragent, char *auth, size_t *num);
