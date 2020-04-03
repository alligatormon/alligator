#pragma once
#include <stdlib.h>
#include <uv.h>
#include "events/client.h"
#include "mbedtls/config.h"
#include "mbedtls/platform.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ssl.h"

typedef struct uvhttp_ssl_client {
    uv_tcp_t tcp;
    uv_buf_t user_read_buf;
    uv_close_cb user_close_cb;
    uv_read_cb user_read_cb;
    uv_write_cb user_write_cb;
    uv_alloc_cb user_alloc_cb;
    uv_connect_cb user_connnect_cb;
    uv_write_t* user_write_req;
    uv_connect_t* user_connnect_req;
    //char *net_buffer_in;
    char ssl_read_buffer[65535];
    unsigned int ssl_read_buffer_len;
    unsigned int ssl_read_buffer_offset;
    unsigned int ssl_write_offset;
    unsigned int ssl_write_buffer_len;
    char* ssl_write_buffer;
    char is_async_writing;
    char is_writing;
    char is_write_error;
    char is_closing;
    uv_buf_t write_buffer;
    mbedtls_ssl_context ssl;
    mbedtls_pk_context key;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cacert;
} uvhttp_ssl_client;
