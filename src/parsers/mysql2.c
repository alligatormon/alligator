#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bio.h>

#include "main.h"
#include "parsers/mysql2.h"
#include "common/logs.h"


typedef enum {
	STATE_HANDSHAKE,
	STATE_SSL_REQUEST,
	STATE_AUTH,
	STATE_QUERY,
	STATE_RESULT,
	STATE_CONNECT_FAILED
} conn_state_t;

typedef struct {
	char host[256];
	int port;
	char user[64];
	char pass[64];
} mysql_uri_t;


struct mysql_conn_t {
	uv_tcp_t handle;
	conn_state_t state;
	SSL *ssl;
	SSL_CTX *ssl_ctx;
	uint8_t seq_id;
	context_arg *carg;
	char *user;
	char *password;
	char auth_plugin[64];
	uint8_t scramble[20];
	int auth_waiting_rsa_key;
	int is_ssl_active;
	int col_count;
	uv_async_t async_query_trigger;
	uv_timer_t query_timer;
	uv_timer_t connect_timer;
	mysql_row_cb on_row;
	void *user_data;
	char *current_query;
	char   **col_names;
	int	 col_names_i;
	uint8_t *col_types;
	uint16_t *col_flags;
};

typedef struct query_context_t {
	uv_mutex_t mutex;
	uv_cond_t cond;
	int done;
	int row_no;
	int metadata_eof_seen;
	int timed_out;
	struct mysql_conn_t *conn;
	mysql_row_cb user_cb;
	void *user_data;
} query_context_t;

typedef struct query_bridge_t {
	mysql_conn_t *conn;
	const char *sql;
	query_context_t *ctx;
} query_bridge_t;


void mysql2_async_query_handler(uv_async_t *handle);
void mysql2_on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);
void mysql2_await_query_context(mysql_conn_t *conn, const char *sql, mysql_row_cb cb, void *user_data);

static void mysql2_alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
	(void)handle;
	buf->base = malloc(suggested_size);
	buf->len = suggested_size;
}

static int mysql2_port_from_carg(context_arg *carg)
{
	if (!carg->port[0])
		return 3306;

	char *end = NULL;
	unsigned long p = strtoul(carg->port, &end, 10);
	if (!end || *end != '\0' || p == 0 || p > 65535) {
		carglog(carg, L_ERROR, "{\"fd\": %d, \"conn\": \"%s\", \"action\": \"mysql2 invalid port\", \"port\": \"%s\"}\n", carg->fd, carg->key ? carg->key : "", carg->port);
		return -1;
	}
	return (int)p;
}

#define QUERY_TIMEOUT_MS 10000
#define CONNECT_TIMEOUT_MS 5000


void parse_mysql_uri(const char *uri, mysql_uri_t *out) {
	out->user[0] = 0;
	out->pass[0] = 0;
	out->host[0] = 0;
	out->port = 3306;
	sscanf(uri, "mysql://%63[^:]:%63[^@]@%255[^:]:%d", out->user, out->pass, out->host, &out->port);
}

void mysql2_on_write_completed(uv_write_t *req, int status) {
	if (req->data)
		free(req->data);
	free(req);
}

void mysql2_on_connect_timeout(uv_timer_t *timer) {
	mysql_conn_t *conn = (mysql_conn_t *)timer->data;
	context_arg *carg = conn->carg;
	carglog(carg, L_ERROR, "MySQL connection timeout\n");
	uv_timer_stop(timer);
	conn->state = STATE_CONNECT_FAILED;
	uv_close((uv_handle_t *)&conn->handle, NULL);
}

static mysql_conn_t *mysql2_get_conn(context_arg *carg, int create_if_null)
{
	if (!carg)
		return NULL;

	if (!carg->data && !create_if_null)
		return NULL;

	if (!carg->data && create_if_null) {
		mysql_conn_t *conn = calloc(1, sizeof(mysql_conn_t));
		if (!conn)
			return NULL;

		static int ssl_inited = 0;
		if (!ssl_inited) {
			SSL_library_init();
			SSL_load_error_strings();
			ssl_inited = 1;
		}

		uv_loop_t *loop = carg->loop;
		if (!loop)
			loop = get_threaded_loop_t_or_default(carg->threaded_loop_name);

		conn->ssl_ctx = SSL_CTX_new(TLS_client_method());
		if (!conn->ssl_ctx) {
			free(conn);
			return NULL;
		}

		uv_tcp_init(loop, &conn->handle);
		conn->handle.data = conn;
		uv_async_init(loop, &conn->async_query_trigger, mysql2_async_query_handler);
		uv_timer_init(loop, &conn->query_timer);
		uv_timer_init(loop, &conn->connect_timer);

		conn->user = strdup(carg->user[0] ? carg->user : "");
		conn->password = strdup(carg->password[0] ? carg->password : "");
		conn->carg = carg;

		carg->data = conn;
	}

	if (carg->data)
		((mysql_conn_t*)carg->data)->carg = carg;

	return (mysql_conn_t*)carg->data;
}


void mysql2_on_connect(uv_connect_t *req, int status) {
	mysql_conn_t *conn = (mysql_conn_t*)req->data;
	free(req);

	uv_timer_stop(&conn->connect_timer);
	context_arg *carg = conn->carg;

	if (status < 0) {
		conn->state = STATE_CONNECT_FAILED;
		if (status == UV_ECANCELED)
			carglog(conn->carg, L_ERROR, "mysql: '%s' Connection timeout\n", conn->carg->host);
		else
			carglog(conn->carg, L_ERROR, "mysql: '%s' Connect error: %s\n", conn->carg->host, uv_err_name(status));
		uv_close((uv_handle_t *)&conn->connect_timer, NULL);
		uv_close((uv_handle_t *)&conn->query_timer, NULL);
		uv_close((uv_handle_t *)&conn->async_query_trigger, NULL);
		return;
	}

	carglog(carg, L_INFO, "TCP Connected to MySQL. Initializing Handshake: '%s'\n", conn->carg->host);
	conn->state = STATE_HANDSHAKE;
	conn->seq_id = 0;

	uv_read_start((uv_stream_t*)&conn->handle, mysql2_alloc_buffer, mysql2_on_read);
}

int mysql2_connect(mysql_conn_t **pconn, context_arg *carg)
{
	if (!pconn || !carg)
		return 0;

	if (!carg->loop)
		carg->loop = get_threaded_loop_t_or_default(carg->threaded_loop_name);

	if (!*pconn) {
		mysql_conn_t *conn = calloc(1, sizeof(mysql_conn_t));
		if (!conn)
			return 0;

		static int ssl_inited = 0;
		if (!ssl_inited) {
			SSL_library_init();
			SSL_load_error_strings();
			ssl_inited = 1;
		}

		conn->ssl_ctx = SSL_CTX_new(TLS_client_method());
		if (!conn->ssl_ctx) {
			free(conn);
			return 0;
		}

		uv_tcp_init(carg->loop, &conn->handle);
		conn->handle.data = conn;
		uv_async_init(carg->loop, &conn->async_query_trigger, mysql2_async_query_handler);
		uv_timer_init(carg->loop, &conn->query_timer);
		uv_timer_init(carg->loop, &conn->connect_timer);

		conn->user = strdup(carg->user[0] ? carg->user : "");
		conn->password = strdup(carg->password[0] ? carg->password : "");
		conn->carg = carg;

		/* calloc -> state=0 which equals STATE_HANDSHAKE; mark as not-started */
		conn->state = (conn_state_t)-1;

		*pconn = conn;
	}

	mysql_conn_t *conn = *pconn;
	if (conn->state == STATE_QUERY)
		return 1;

	int port = mysql2_port_from_carg(carg);
	if (port < 0) {
		conn->state = STATE_CONNECT_FAILED;
		return 0;
	}

	struct sockaddr_in dest;
	uv_ip4_addr(carg->host, port, &dest);

	uv_connect_t *connect_req = malloc(sizeof(uv_connect_t));
	if (!connect_req)
		return 0;
	connect_req->data = conn;

	conn->state = STATE_HANDSHAKE;
	conn->seq_id = 0;
	conn->carg = carg;

	conn->connect_timer.data = conn;
	uv_timer_start(&conn->connect_timer, mysql2_on_connect_timeout, CONNECT_TIMEOUT_MS, 0);

	int r = uv_tcp_connect(connect_req, &conn->handle, (const struct sockaddr*)&dest, mysql2_on_connect);
	if (r < 0) {
		free(connect_req);
		return 0;
	}

	while (conn->state != STATE_QUERY && conn->state != STATE_CONNECT_FAILED)
		uv_sleep(10);

	return conn->state == STATE_QUERY;
}

void mysql2_await_query(mysql_conn_t *conn,
						const char *sql,
						mysql_row_cb cb,
						void *user_data)
{
	if (!conn || conn->state != STATE_QUERY)
		return;

	mysql2_await_query_context(conn, sql, cb, user_data);
}

int mysql2_is_ready(mysql_conn_t *conn)
{
	return conn && conn->state == STATE_QUERY;
}

int mysql2_is_failed(mysql_conn_t *conn)
{
	return conn && conn->state == STATE_CONNECT_FAILED;
}

int mysql2_start_connect(mysql_conn_t **pconn, context_arg *carg)
{
	if (!pconn || !carg)
		return 0;

	if (!carg->loop)
		carg->loop = get_threaded_loop_t_or_default(carg->threaded_loop_name);

	if (!*pconn) {
		mysql_conn_t *conn = calloc(1, sizeof(mysql_conn_t));
		if (!conn)
			return 0;

		static int ssl_inited = 0;
		if (!ssl_inited) {
			SSL_library_init();
			SSL_load_error_strings();
			ssl_inited = 1;
		}

		conn->ssl_ctx = SSL_CTX_new(TLS_client_method());
		if (!conn->ssl_ctx) {
			free(conn);
			return 0;
		}

		uv_tcp_init(carg->loop, &conn->handle);
		conn->handle.data = conn;
		uv_async_init(carg->loop, &conn->async_query_trigger, mysql2_async_query_handler);
		uv_timer_init(carg->loop, &conn->query_timer);
		uv_timer_init(carg->loop, &conn->connect_timer);

		conn->user = strdup(carg->user[0] ? carg->user : "");
		conn->password = strdup(carg->password[0] ? carg->password : "");

		/* calloc -> state=0 which equals STATE_HANDSHAKE; mark as not-started */
		conn->state = (conn_state_t)-1;

		*pconn = conn;
	}

	mysql_conn_t *conn = *pconn;
	if (conn->state == STATE_QUERY)
		return 1;
	if (conn->state == STATE_HANDSHAKE || conn->state == STATE_AUTH)
		return 1;

	int port = mysql2_port_from_carg(carg);
	if (port < 0) {
		conn->state = STATE_CONNECT_FAILED;
		return 0;
	}

	struct sockaddr_in dest;
	uv_ip4_addr(carg->host, port, &dest);

	uv_connect_t *connect_req = malloc(sizeof(uv_connect_t));
	if (!connect_req)
		return 0;
	connect_req->data = conn;

	conn->state = STATE_HANDSHAKE;
	conn->seq_id = 0;
	conn->carg = carg;

	conn->connect_timer.data = conn;
	uv_timer_start(&conn->connect_timer, mysql2_on_connect_timeout, CONNECT_TIMEOUT_MS, 0);

	int r = uv_tcp_connect(connect_req, &conn->handle, (const struct sockaddr*)&dest, mysql2_on_connect);
	if (r < 0) {
		free(connect_req);
		return 0;
	}

	return 1;
}

int mysql2_connect_context(context_arg *carg)
{
	mysql_conn_t *conn = mysql2_get_conn(carg, 1);
	if (!conn)
		return 0;

	if (conn->state == STATE_QUERY)
		return 1;

	int port = mysql2_port_from_carg(carg);
	if (port < 0) {
		conn->state = STATE_CONNECT_FAILED;
		return 0;
	}

	struct sockaddr_in dest;
	uv_ip4_addr(carg->host, port, &dest);

	uv_connect_t *connect_req = malloc(sizeof(uv_connect_t));
	if (!connect_req)
		return 0;
	connect_req->data = conn;
	conn->carg = carg;

	conn->state = STATE_HANDSHAKE;
	conn->seq_id = 0;

	conn->connect_timer.data = conn;
	uv_timer_start(&conn->connect_timer, mysql2_on_connect_timeout, CONNECT_TIMEOUT_MS, 0);

	int r = uv_tcp_connect(connect_req, &conn->handle, (const struct sockaddr*)&dest, mysql2_on_connect);
	if (r < 0) {
		free(connect_req);
		return 0;
	}

	while (conn->state != STATE_QUERY && conn->state != STATE_CONNECT_FAILED)
		uv_sleep(10);

	return conn->state == STATE_QUERY;
}



static void mysql2_send_mysql_packet(mysql_conn_t *conn, const uint8_t *payload, size_t payload_len) {
	size_t total_len = 4 + payload_len;
	uint8_t *pkt = malloc(total_len);
	if (!pkt) return;
	conn->seq_id++;
	pkt[0] = (uint8_t)(payload_len & 0xFF);
	pkt[1] = (uint8_t)((payload_len >> 8) & 0xFF);
	pkt[2] = (uint8_t)((payload_len >> 16) & 0xFF);
	pkt[3] = conn->seq_id;
	memcpy(pkt + 4, payload, payload_len);
	uv_buf_t buf = uv_buf_init((char *)pkt, total_len);
	uv_write_t *req = malloc(sizeof(uv_write_t));
	if (!req) {
		free(pkt);
		return;
	}
	req->data = pkt;
	uv_write(req, (uv_stream_t *)&conn->handle, &buf, 1, mysql2_on_write_completed);
}


void send_mysql_packet_immediate(mysql_conn_t *conn, const uint8_t *payload, size_t payload_len) {
	size_t total_len = 4 + payload_len;
	if (total_len > 256) {
		mysql2_send_mysql_packet(conn, payload, payload_len);
		return;
	}
	uint8_t buf[260];
	buf[0] = (uint8_t)(payload_len & 0xFF);
	buf[1] = (uint8_t)((payload_len >> 8) & 0xFF);
	buf[2] = (uint8_t)((payload_len >> 16) & 0xFF);

	conn->seq_id++;
	buf[3] = conn->seq_id;
	memcpy(buf + 4, payload, payload_len);
	uv_buf_t b = uv_buf_init((char *)buf, total_len);
	int n = uv_try_write((uv_stream_t *)&conn->handle, &b, 1);
	if (n < 0 || (size_t)n < total_len) {
		uint8_t *pkt = malloc(total_len);
		if (pkt) {
			memcpy(pkt, buf, total_len);
			uv_buf_t wb = uv_buf_init((char *)pkt, total_len);
			uv_write_t *req = malloc(sizeof(uv_write_t));
			if (!req) {
				free(pkt);
				return;
			}
			req->data = pkt;
			uv_write(req, (uv_stream_t *)&conn->handle, &wb, 1, mysql2_on_write_completed);
		}
	}
}

static int send_rsa_encrypted_password(mysql_conn_t *conn, const char *pem_key, size_t pem_len, const uint8_t *scramble20) {
	char *pem_copy = malloc(pem_len + 1);
	if (!pem_copy) return 0;
	memcpy(pem_copy, pem_key, pem_len);
	pem_copy[pem_len] = '\0';
	BIO *bio = BIO_new_mem_buf(pem_copy, (int)pem_len + 1);
	if (!bio) { free(pem_copy); return 0; }
	EVP_PKEY *pkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
	BIO_free(bio);
	free(pem_copy);
	if (!pkey) return 0;

	EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(pkey, NULL);
	EVP_PKEY_free(pkey);
	if (!ctx) return 0;
	if (EVP_PKEY_encrypt_init(ctx) <= 0) {
		EVP_PKEY_CTX_free(ctx);
		return 0;
	}
	if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) <= 0) {
		EVP_PKEY_CTX_free(ctx);
		return 0;
	}

	size_t plain_len = strlen(conn->password) + 1;
	unsigned char *plain = malloc(plain_len);
	if (!plain) { EVP_PKEY_CTX_free(ctx); return 0; }
	for (size_t i = 0; i < plain_len; i++) {
		unsigned char c = (i < plain_len - 1) ? (unsigned char)conn->password[i] : 0;
		plain[i] = c ^ scramble20[i % 20];
	}
	size_t out_len = 0;
	if (EVP_PKEY_encrypt(ctx, NULL, &out_len, plain, plain_len) <= 0) {
		EVP_PKEY_CTX_free(ctx);
		free(plain);
		return 0;
	}
	unsigned char *encrypted = malloc(out_len);
	if (!encrypted) { EVP_PKEY_CTX_free(ctx); free(plain); return 0; }
	if (EVP_PKEY_encrypt(ctx, encrypted, &out_len, plain, plain_len) <= 0) {
		EVP_PKEY_CTX_free(ctx);
		free(encrypted);
		free(plain);
		return 0;
	}
	EVP_PKEY_CTX_free(ctx);
	free(plain);
	mysql2_send_mysql_packet(conn, encrypted, out_len);
	free(encrypted);
	return 1;
}


static void mysql2_free_metadata(mysql_conn_t *conn)
{
	if (!conn)
		return;

	if (conn->col_names) {
		for (int i = 0; i < conn->col_count; i++)
			free(conn->col_names[i]);
		free(conn->col_names);
		conn->col_names = NULL;
	}

	free(conn->col_types);
	conn->col_types = NULL;
	free(conn->col_flags);
	conn->col_flags = NULL;

	conn->col_names_i = 0;
}

void send_query(mysql_conn_t *conn, const char *sql, mysql_row_cb cb, void *user_data) {
	mysql2_free_metadata(conn);
	conn->col_count = 0;

	conn->on_row = cb;
	conn->user_data = user_data;
	conn->current_query = (char*)sql;

	size_t sql_len = strlen(sql);
	size_t payload_len = 1 + sql_len;
	size_t total_len = 4 + payload_len;

	uint8_t *pkt = malloc(total_len);
	pkt[0] = payload_len & 0xFF;
	pkt[1] = (payload_len >> 8) & 0xFF;
	pkt[2] = (payload_len >> 16) & 0xFF;
	pkt[3] = 0;
	pkt[4] = 0x03;
	memcpy(pkt + 5, sql, sql_len);

	uv_buf_t buf = uv_buf_init((char*)pkt, total_len);
	uv_write_t *req = malloc(sizeof(uv_write_t));
	if (!req) {
		free(pkt);
		return;
	}
	req->data = pkt;

	uv_write(req, (uv_stream_t*)&conn->handle, &buf, 1, mysql2_on_write_completed);
}

void mysql2_await_query_context(mysql_conn_t *conn, const char *sql, mysql_row_cb cb, void *user_data) {
	query_context_t *ctx = calloc(1, sizeof(query_context_t));
	uv_mutex_init(&ctx->mutex);
	uv_cond_init(&ctx->cond);
	ctx->done = 0;
	ctx->timed_out = 0;
	ctx->conn = conn;
	ctx->user_cb = cb;
	ctx->user_data = user_data;

	query_bridge_t *bridge = malloc(sizeof(query_bridge_t));
	bridge->conn = conn;
	bridge->sql = sql;
	bridge->ctx = ctx;

	conn->async_query_trigger.data = bridge;
	uv_async_send(&conn->async_query_trigger);

	uv_mutex_lock(&ctx->mutex);
	while (!ctx->done) {
		uv_cond_wait(&ctx->cond, &ctx->mutex);
	}
	uv_mutex_unlock(&ctx->mutex);

	context_arg *carg = (context_arg *)conn->user_data;
	if (ctx->timed_out)
		carglog(carg, L_ERROR, "await_query: timeout for %s\n", sql);

	uv_mutex_destroy(&ctx->mutex);
	uv_cond_destroy(&ctx->cond);
	free(ctx);
}

void mysql2_native_password_hash(const char *password, const uint8_t *scramble, uint8_t *out) {
	if (!password || strlen(password) == 0) return;

	unsigned char hash_stage1[20]; // SHA1_DIGEST_LENGTH
	unsigned char hash_stage2[20];
	unsigned char hash_stage3[20];
	unsigned int md_len;

	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	const EVP_MD *md = EVP_sha1();

	// 1. SHA1(password) -> hash_stage1
	EVP_DigestInit_ex(ctx, md, NULL);
	EVP_DigestUpdate(ctx, password, strlen(password));
	EVP_DigestFinal_ex(ctx, hash_stage1, &md_len);

	// 2. SHA1(hash_stage1) -> hash_stage2
	EVP_DigestInit_ex(ctx, md, NULL);
	EVP_DigestUpdate(ctx, hash_stage1, 20);
	EVP_DigestFinal_ex(ctx, hash_stage2, &md_len);

	// 3. SHA1(scramble + hash_stage2) -> hash_stage3
	EVP_DigestInit_ex(ctx, md, NULL);
	EVP_DigestUpdate(ctx, scramble, 20);
	EVP_DigestUpdate(ctx, hash_stage2, 20);
	EVP_DigestFinal_ex(ctx, hash_stage3, &md_len);

	EVP_MD_CTX_free(ctx);

	// 4. XOR
	for (int i = 0; i < 20; i++) {
		out[i] = hash_stage1[i] ^ hash_stage3[i];
	}
}

/* caching_sha2_password: token = XOR(SHA256(password), SHA256(scramble || SHA256(SHA256(password)))) */
static void mysql2_caching_sha2_password_hash(const char *password, const uint8_t *scramble, uint8_t *out) {
	if (!password || strlen(password) == 0) return;

	const EVP_MD *md = EVP_sha256();
	unsigned char digest1[32], digest2[32], digest3[32];
	unsigned int md_len;
	EVP_MD_CTX *ctx = EVP_MD_CTX_new();

	EVP_DigestInit_ex(ctx, md, NULL);
	EVP_DigestUpdate(ctx, password, strlen(password));
	EVP_DigestFinal_ex(ctx, digest1, &md_len);

	EVP_DigestInit_ex(ctx, md, NULL);
	EVP_DigestUpdate(ctx, digest1, 32);
	EVP_DigestFinal_ex(ctx, digest2, &md_len);

	EVP_DigestInit_ex(ctx, md, NULL);
	EVP_DigestUpdate(ctx, scramble, 20);
	EVP_DigestUpdate(ctx, digest2, 32);
	EVP_DigestFinal_ex(ctx, digest3, &md_len);
	EVP_MD_CTX_free(ctx);

	for (int i = 0; i < 32; i++)
		out[i] = digest1[i] ^ digest3[i];
}

void mysql2_send_auth_packet(mysql_conn_t *conn, const uint8_t *scramble) {
	size_t user_len = strlen(conn->user);
	size_t plugin_len = 0;
	const char *plugin_name = (strcmp(conn->auth_plugin, "caching_sha2_password") == 0)
		? "caching_sha2_password" : "mysql_native_password";
	plugin_len = strlen(plugin_name);
	size_t auth_len = (conn->password && strlen(conn->password) > 0)
		? ((strcmp(conn->auth_plugin, "caching_sha2_password") == 0) ? 1 + 32 : 1 + 20)
		: 1;
	size_t payload_len = 4 + 4 + 1 + 23 + user_len + 1 + auth_len + plugin_len + 1;
	uint8_t *payload = malloc(payload_len);
	uint8_t *ptr = payload;

	// CLIENT_LONG_PASSWORD | CLIENT_PROTOCOL_41 | CLIENT_SECURE_CONNECTION | CLIENT_PLUGIN_AUTH
	uint32_t caps = 0x00000001 | 0x00000200 | 0x00008000 | 0x00080000;

	memcpy(ptr, &caps, 4); ptr += 4;

	uint32_t max_size = 0x01000000;
	memcpy(ptr, &max_size, 4); ptr += 4;

	*ptr = 33; ptr++;

	memset(ptr, 0, 23); ptr += 23;

	memcpy(ptr, conn->user, user_len);
	ptr += user_len;
	*ptr++ = 0;

	if (conn->password && strlen(conn->password) > 0) {
		int use_caching_sha2 = (strcmp(conn->auth_plugin, "caching_sha2_password") == 0);
		if (use_caching_sha2) {
			uint8_t token[32];
			mysql2_caching_sha2_password_hash(conn->password, scramble, token);
			*ptr = 32;
			ptr++;
			memcpy(ptr, token, 32);
			ptr += 32;
		} else {
			uint8_t hashed_pw[20];
			mysql2_native_password_hash(conn->password, scramble, hashed_pw);
			*ptr = 20;
			ptr++;
			memcpy(ptr, hashed_pw, 20);
			ptr += 20;
		}
	} else {
		*ptr = 0;
		ptr++;
	}

	memcpy(ptr, plugin_name, plugin_len);
	ptr += plugin_len;
	*ptr++ = 0;

	payload_len = (size_t)(ptr - payload);
	size_t total_len = payload_len + 4;
	uint8_t *full_pkt = malloc(total_len);

	conn->seq_id++;
	full_pkt[0] = (uint8_t)(payload_len & 0xFF);
	full_pkt[1] = (uint8_t)((payload_len >> 8) & 0xFF);
	full_pkt[2] = (uint8_t)((payload_len >> 16) & 0xFF);
	full_pkt[3] = conn->seq_id;

	memcpy(full_pkt + 4, payload, payload_len);
	free(payload);

	uv_buf_t buf = uv_buf_init((char*)full_pkt, total_len);
	uv_write_t *req = malloc(sizeof(uv_write_t));
	if (!req) {
		free(full_pkt);
		return;
	}
	req->data = full_pkt;

	int r = uv_write(req, (uv_stream_t*)&conn->handle, &buf, 1, mysql2_on_write_completed);
	if (r < 0) {
		carglog(conn->carg, L_ERROR, "mysql: '%s' Write error: %s\n", conn->carg->host, uv_strerror(r));
	}
}

uint64_t mysql2_read_lei_safe(uint8_t **ptr, uint8_t *end) {
	if (*ptr >= end) return 0;

	uint8_t first = **ptr;
	(*ptr)++;

	if (first < 0xFB) return first;

	if (first == 0xFC) {
		if (*ptr + 2 > end) return 0;
		uint16_t v = (*ptr)[0] | ((*ptr)[1] << 8);
		*ptr += 2; return v;
	}

	if (first == 0xFD) {
		if (*ptr + 3 > end) return 0;
		uint32_t v = (*ptr)[0] | ((*ptr)[1] << 8) | ((*ptr)[2] << 16);
		*ptr += 3; return v;
	}

	return 0;
}

static int mysql2_skip_lenenc_str(uint8_t **pp, uint8_t *end)
{
	if (*pp >= end)
		return -1;
	if (**pp == 0xFB) {
		(*pp)++;
		return 0;
	}
	uint64_t len = mysql2_read_lei_safe(pp, end);
	if (*pp + len > end)
		return -1;
	*pp += (size_t)len;
	return 0;
}

static char *mysql2_dup_lenenc_str(uint8_t **pp, uint8_t *end)
{
	if (*pp >= end)
		return NULL;
	if (**pp == 0xFB) {
		(*pp)++;
		return strdup("");
	}
	uint64_t len = mysql2_read_lei_safe(pp, end);
	if (*pp + len > end)
		return NULL;
	char *s = malloc(len + 1);
	if (!s)
		return NULL;
	memcpy(s, *pp, (size_t)len);
	s[len] = '\0';
	*pp += (size_t)len;
	return s;
}

static char *mysql2_column_def_display_name(const uint8_t *payload, size_t pkt_len)
{
	uint8_t *p = (uint8_t *)payload;
	uint8_t *end = p + pkt_len;
	if (mysql2_skip_lenenc_str(&p, end) < 0)
		return NULL;
	if (mysql2_skip_lenenc_str(&p, end) < 0)
		return NULL;
	if (mysql2_skip_lenenc_str(&p, end) < 0)
		return NULL;
	if (mysql2_skip_lenenc_str(&p, end) < 0)
		return NULL;
	return mysql2_dup_lenenc_str(&p, end);
}

static int mysql2_column_def_type_flags(const uint8_t *payload, size_t pkt_len, uint8_t *out_type, uint16_t *out_flags)
{
	uint8_t *p = (uint8_t *)payload;
	uint8_t *end = p + pkt_len;

	/* skip: catalog, schema, table, org_table, name, org_name */
	for (int i = 0; i < 6; i++) {
		if (mysql2_skip_lenenc_str(&p, end) < 0)
			return 0;
	}

	if (p >= end)
		return 0;

	/* length of fixed fields, should be 0x0c */
	uint64_t fixed_len = mysql2_read_lei_safe(&p, end);
	if (fixed_len < 0x0c)
		return 0;
	if (p + 0x0c > end)
		return 0;

	p += 2; /* charset */
	p += 4; /* column_length */
	uint8_t t = *p++;
	uint16_t fl = (uint16_t)(p[0] | (p[1] << 8));

	if (out_type)
		*out_type = t;
	if (out_flags)
		*out_flags = fl;
	return 1;
}

void mysql2_parse_universal_result(mysql_conn_t *conn, uint8_t *data, size_t len, mysql_row_cb cb, void *user_data) {
	uint8_t *p = data;
	uint8_t *end = data + len;
	query_context_t *ctx = (query_context_t *)user_data;

	while (p + 4 <= end) {
		uint32_t pkt_len = p[0] | (p[1] << 8) | (p[2] << 16);
		uint8_t pkt_seq = p[3];
		uint8_t *payload = p + 4;

		if (p + 4 + pkt_len > end) break;

		if (pkt_seq == 1 && payload[0] == 0x00) {
			if (ctx) {
				mysql2_free_metadata(conn);
				uv_timer_stop(&conn->query_timer);
				uv_mutex_lock(&ctx->mutex);
				ctx->done = 1;
				uv_cond_signal(&ctx->cond);
				uv_mutex_unlock(&ctx->mutex);
				conn->user_data = NULL;
				conn->on_row = NULL;
				ctx = NULL;
			}
			return;
		}

		if (payload[0] == 0xFF) {
			uint16_t err_code = payload[1] | (payload[2] << 8);
			char *msg = (char *)(payload + 9);
			int msg_len = pkt_len - 9;
			carglog(conn->carg, L_ERROR, "mysql: '%s' >>> MySQL ERROR %d: %.*s\n", conn->carg->host, err_code, msg_len, msg);

			if (ctx) {
				mysql2_free_metadata(conn);
				uv_timer_stop(&conn->query_timer);
				uv_mutex_lock(&ctx->mutex);
				ctx->done = 1;
				uv_cond_signal(&ctx->cond);
				uv_mutex_unlock(&ctx->mutex);
				conn->user_data = NULL;
				conn->on_row = NULL;
				ctx = NULL;
			}
			return;
		}

		if (pkt_seq == 1) {
			uint8_t *tmp = payload;
			conn->col_count = (int)mysql2_read_lei_safe(&tmp, payload + pkt_len);
		}
		else if (payload[0] != 0xFE && pkt_seq > 1) {
			if (pkt_len >= 5 && payload[0] == 0x03 && memcmp(payload + 1, "def", 3) == 0) {
				char *disp = mysql2_column_def_display_name(payload, pkt_len);
				uint8_t ctype = 0;
				uint16_t cflags = 0;
				int has_tf = mysql2_column_def_type_flags(payload, pkt_len, &ctype, &cflags);
				if (disp && conn->col_count > 0) {
					if (!conn->col_names)
						conn->col_names = calloc((size_t)conn->col_count, sizeof(char *));
					if (!conn->col_types)
						conn->col_types = calloc((size_t)conn->col_count, sizeof(uint8_t));
					if (!conn->col_flags)
						conn->col_flags = calloc((size_t)conn->col_count, sizeof(uint16_t));

					if (conn->col_names && conn->col_names_i < conn->col_count) {
						int idx = conn->col_names_i++;
						conn->col_names[idx] = disp;
						if (has_tf && conn->col_types)
							conn->col_types[idx] = ctype;
						if (has_tf && conn->col_flags)
							conn->col_flags[idx] = cflags;
					}
					else
						free(disp);
				} else if (disp)
					free(disp);
			} else {
				if (conn->col_count <= 0 || conn->col_count > 1024) {
					p += pkt_len + 4;
					continue;
				}
				uint8_t **fields = calloc((size_t)conn->col_count, sizeof(*fields));
				uint64_t *lengths = calloc((size_t)conn->col_count, sizeof(*lengths));
				if (!fields || !lengths) {
					free(fields);
					free(lengths);
					p += pkt_len + 4;
					continue;
				}
				uint8_t *f_ptr = payload;
				uint8_t *r_end = payload + pkt_len;
				int ok = conn->col_count > 0;

				for (int i = 0; i < conn->col_count; i++) {
					if (f_ptr >= r_end) { ok = 0; break; }
					lengths[i] = mysql2_read_lei_safe(&f_ptr, r_end);
					fields[i] = f_ptr;
					f_ptr += lengths[i];
				}

				if (ok && ctx && ctx->user_cb) {
					mysql_row_t row = { fields, lengths, conn->col_count, ctx->row_no++,
										(char **)conn->col_names,
										conn->col_types,
										conn->col_flags };
					ctx->user_cb(&row, ctx->user_data);
				}
				free(fields);
				free(lengths);
			}
		}

		if (payload[0] == 0xFE && pkt_len < 9) {
			if (ctx && !ctx->metadata_eof_seen) {
				ctx->metadata_eof_seen = 1;
			} else if (ctx) {
				mysql2_free_metadata(conn);
				uv_timer_stop(&conn->query_timer);
				uv_mutex_lock(&ctx->mutex);
				ctx->done = 1;
				uv_cond_signal(&ctx->cond);
				uv_mutex_unlock(&ctx->mutex);
				conn->user_data = NULL;
				conn->on_row = NULL;
				ctx = NULL;
			}
		}

		p += pkt_len + 4;
	}
}


void mysql2_on_query_timeout(uv_timer_t *timer) {
	query_context_t *ctx = (query_context_t *)timer->data;
	mysql_conn_t *conn = ctx->conn;
	uv_timer_stop(timer);
	mysql2_free_metadata(conn);
	uv_mutex_lock(&ctx->mutex);
	ctx->done = 1;
	ctx->timed_out = 1;
	uv_cond_signal(&ctx->cond);
	uv_mutex_unlock(&ctx->mutex);
	conn->user_data = NULL;
	conn->on_row = NULL;
}

void mysql2_async_query_handler(uv_async_t *handle) {
	query_bridge_t *bridge = (query_bridge_t *)handle->data;

	if (!bridge->sql) {
		mysql_conn_t *conn = bridge->conn;
		mysql2_free_metadata(conn);
		free(bridge);
		uv_timer_stop(&conn->query_timer);
		uv_timer_stop(&conn->connect_timer);
		uv_close((uv_handle_t*)&conn->query_timer, NULL);
		uv_close((uv_handle_t*)&conn->connect_timer, NULL);
		uv_read_stop((uv_stream_t*)&conn->handle);
		uv_close((uv_handle_t*)&conn->handle, NULL);
		uv_close((uv_handle_t*)&conn->async_query_trigger, NULL);
		return;
	}

	send_query(bridge->conn, bridge->sql, (mysql_row_cb)mysql2_parse_universal_result, bridge->ctx);
	bridge->conn->query_timer.data = bridge->ctx;
	uv_timer_start(&bridge->conn->query_timer, mysql2_on_query_timeout, QUERY_TIMEOUT_MS, 0);
	free(bridge);
}

void mysql2_on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
	mysql_conn_t *conn = (mysql_conn_t*)stream->data;

	if (nread < 0) {
		carglog(conn->carg, L_ERROR, "Read error or connection closed\n");
		uv_close((uv_handle_t*)stream, NULL);
		return;
	}

	if (nread > 0) {
		uint8_t *data = (uint8_t*)buf->base;
		size_t left = (size_t)nread;
		uint8_t *p = data;
		int auth_just_finished = 0;

		if (conn->state != STATE_QUERY) {
		while (left >= 4) {
			uint32_t pkt_len = p[0] | (p[1] << 8) | (p[2] << 16);
			if (left < 4 + pkt_len) break;
			uint8_t pkt_seq = p[3];
			uint8_t *payload = p + 4;

			conn->seq_id = pkt_seq;

			if (conn->state == STATE_HANDSHAKE) {
				uint8_t scramble[20];
				uint8_t *pos = p + 5;
				while (pos < p + 4 + pkt_len && *pos != 0x00) pos++;
				if (pos >= p + 4 + pkt_len) break;
				pos++;
				pos += 4;
				if (pos + 8 > p + 4 + pkt_len) break;

				memcpy(scramble, pos, 8);
				pos += 8 + 1 + 2 + 1 + 2 + 2 + 1 + 10;
				if (pos + 12 > p + 4 + pkt_len) break;
				memcpy(scramble + 8, pos, 12);
				memcpy(conn->scramble, scramble, 20);
				pos += 12;
				if (pos < p + 4 + pkt_len && *pos == 0) pos++;
				conn->auth_plugin[0] = '\0';
				if (pos < p + 4 + pkt_len && *pos) {
					size_t plen = strnlen((char *)pos, (size_t)(p + 4 + pkt_len - pos));
					if (plen >= sizeof(conn->auth_plugin)) plen = sizeof(conn->auth_plugin) - 1;
					memcpy(conn->auth_plugin, pos, plen);
					conn->auth_plugin[plen] = '\0';
				}
				if (!conn->auth_plugin[0])
					strcpy(conn->auth_plugin, "mysql_native_password");

				mysql2_send_auth_packet(conn, scramble);
				conn->state = STATE_AUTH;
			}
			else if (conn->state == STATE_AUTH) {
				if (conn->auth_waiting_rsa_key) {
					if (pkt_len > 50 && conn->password && strlen(conn->password) > 0) {
						const char *pem = (const char *)payload;
						size_t pem_len = pkt_len;
						if (payload[0] == 0x01) {
							pem++;
							pem_len--;
						}
						if (pem_len > 50 && send_rsa_encrypted_password(conn, pem, pem_len, conn->scramble))
							conn->auth_waiting_rsa_key = 0;
					}
				} else if (payload[0] == 0x00) {
					carglog(conn->carg, L_INFO, "mysql: '%s' Auth Successful!\n", conn->carg->host);
					conn->state = STATE_QUERY;
					auth_just_finished = 1;
					p += 4 + pkt_len;
					left -= 4 + pkt_len;
					break;
				} else if (payload[0] == 0xFF) {
					carglog(conn->carg, L_ERROR, "mysql: '%s' Auth Failed! Error: %s\n", conn->carg->host, (char *)(payload + 9));
					uv_close((uv_handle_t*)stream, NULL);
					if (buf->base) free(buf->base);
					return;
				} else if (payload[0] == 0x01 && pkt_len <= 6 && strcmp(conn->auth_plugin, "caching_sha2_password") == 0) {
					static const uint8_t req_key = 0x02;
					mysql2_send_mysql_packet(conn, &req_key, 1);
					conn->auth_waiting_rsa_key = 1;
				} else if (payload[0] == 0xFE && strcmp(conn->auth_plugin, "caching_sha2_password") == 0) {
					const char *pl = (const char *)payload + 1;
					const char *end_pl = (const char *)payload + pkt_len;
					while (pl < end_pl && *pl) pl++;
					if (pl < end_pl) pl++;
					while (pl < end_pl && (unsigned char)*pl < 0x20) pl++;
					size_t data_len = (size_t)(end_pl - pl);
					if (data_len > 20 && send_rsa_encrypted_password(conn, pl, data_len, conn->scramble)) {
					} else {
						static const uint8_t req_key = 0x02;
						mysql2_send_mysql_packet(conn, &req_key, 1);
						conn->auth_waiting_rsa_key = 1;
					}
				}
			}
			p += 4 + pkt_len;
			left -= 4 + pkt_len;
		}
		}

		context_arg *carg = conn->carg;
		if (conn->state == STATE_QUERY && !auth_just_finished) {
			carglog(carg, L_INFO, "\tMySQL Parsing Result Set: %p: %s, ctx: %p\n", conn->on_row, conn->current_query ? conn->current_query : "unknown", conn->user_data);
			mysql2_parse_universal_result(conn, data, (size_t)nread, conn->on_row, conn->user_data);
		}
	}

	if (buf->base) free(buf->base);
}



