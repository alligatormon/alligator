#pragma once
#include <stddef.h>
#include <stdint.h>
#include <uv.h>

#define PROXY_HS_SIZE 2048
#define PROXY_HOST_SIZE 232
#define PROXY_PORT_SIZE 6

struct context_arg;

typedef enum proxy_type {
	PROXY_TYPE_NONE = 0,
	PROXY_TYPE_HTTP = 1,
	PROXY_TYPE_SOCKS5 = 2,
} proxy_type;

typedef enum proxy_phase {
	PROXY_PHASE_NONE = 0,
	PROXY_PHASE_HTTP_CONNECT_READ,
	PROXY_PHASE_SOCKS5_GREET_READ,
	PROXY_PHASE_SOCKS5_AUTH_READ,
	PROXY_PHASE_SOCKS5_CMD_READ,
	PROXY_PHASE_DONE,
} proxy_phase;

typedef struct proxy_settings {
	proxy_type type;
	uint8_t remote_dns;
	char host[PROXY_HOST_SIZE];
	char port[PROXY_PORT_SIZE];
	uint16_t numport;
	char *user;
	char *password;
	char *auth_b64;
	char *url;
} proxy_settings;

proxy_settings *proxy_parse_url(const char *url);
proxy_settings *proxy_settings_copy(const proxy_settings *src);
void proxy_settings_free(proxy_settings *p);

int proxy_host_is_ipv4(const char *host);
int proxy_ok_for_transport(const proxy_settings *p, uint8_t transport);
int proxy_needs_tunnel(struct context_arg *carg);

char *proxy_http_connect_build(const char *host, uint16_t port, const char *auth_b64, size_t *out_len);
int proxy_http_connect_parse(const char *buf, size_t len, int *http_code, size_t *consumed);

size_t proxy_socks5_greet_build(unsigned char *dst, size_t cap, int with_pass);
size_t proxy_socks5_auth_build(unsigned char *dst, size_t cap, const char *user, const char *pass);
size_t proxy_socks5_cmd_build(unsigned char *dst, size_t cap, uint8_t cmd, const char *host, uint16_t port);
int proxy_socks5_reply_needed(const unsigned char *buf, size_t len, size_t *need);
int proxy_socks5_reply_parse(const unsigned char *buf, size_t len, int *rep, struct sockaddr_in *bind_addr);

size_t proxy_udp_wrap(unsigned char *dst, size_t cap, const char *host, uint16_t port, const unsigned char *payload, size_t payload_len);
int proxy_udp_unwrap(const unsigned char *src, size_t src_len, const unsigned char **payload, size_t *payload_len);

int http_request_apply_proxy(struct context_arg *carg);

void proxy_handshake_start(struct context_arg *carg);
void proxy_client_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);
void proxy_handshake_reset(struct context_arg *carg);
void proxy_fill_udp_relay(struct context_arg *carg, const struct sockaddr_in *bind_addr);
