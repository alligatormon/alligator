#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include "main.h"
#include "common/logs.h"
#include "parsers/cassandra2.h"

#define CASS_OP_ERROR		0x00
#define CASS_OP_STARTUP	  0x01
#define CASS_OP_READY		0x02
#define CASS_OP_AUTHENTICATE 0x03
#define CASS_OP_OPTIONS	  0x05
#define CASS_OP_SUPPORTED	0x06
#define CASS_OP_QUERY		0x07
#define CASS_OP_RESULT	   0x08
#define CASS_OP_AUTH_RESPONSE 0x0f
#define CASS_OP_AUTH_SUCCESS 0x10

#define CASS_RESULT_VOID 0x00000001
#define CASS_RESULT_ROWS 0x00000002

#define CASS_STATE_CONNECTING 0
#define CASS_STATE_HANDSHAKE  1
#define CASS_STATE_READY	  2
#define CASS_STATE_FAILED	 3
#define CASS_STATE_IDLE	   4

#define CASS_CONNECT_TIMEOUT_MS 5000
#define CASS_QUERY_TIMEOUT_MS 10000
#define CASS_MAX_FRAME (16 * 1024 * 1024)

typedef struct cass_query_ctx_t {
	uv_mutex_t mutex;
	uv_cond_t cond;
	int done;
	int timed_out;
	int ok;
	struct cassandra_conn_t *conn;
	cassandra_row_cb cb;
	void *user_data;
	int row_no;
} cass_query_ctx_t;

struct cassandra_conn_t {
	uv_tcp_t handle;
	uv_timer_t connect_timer;
	uv_timer_t query_timer;

	int state;
	int ready_proto; /* 4 or 5 */
	int requested_proto;
	int startup_sent;
	int auth_sent;
	int options_sent;

	int stream_id;
	int current_stream;
	context_arg *carg;
	char user[AUTH_SIZE];
	char password[AUTH_SIZE];

	uint8_t *rbuf;
	size_t rbuf_len;
	size_t rbuf_cap;

	char **col_names;
	uint16_t *col_types;
	int col_count;

	cass_query_ctx_t *active_query;
};

static cassandra_conn_t *cass2_conn_new(uv_loop_t *loop, int requested_proto) {
	cassandra_conn_t *c = calloc(1, sizeof(*c));
	if (!c)
		return NULL;
	uv_tcp_init(loop, &c->handle);
	uv_timer_init(loop, &c->connect_timer);
	uv_timer_init(loop, &c->query_timer);
	c->handle.data = c;
	c->connect_timer.data = c;
	c->query_timer.data = c;
	c->stream_id = 1;
	c->requested_proto = requested_proto;
	c->state = CASS_STATE_IDLE;
	return c;
}

static int cass2_port_from_carg(context_arg *carg) {
	if (!carg->port[0])
		return 9042;
	char *end = NULL;
	unsigned long p = strtoul(carg->port, &end, 10);
	if (!end || *end != '\0' || p == 0 || p > 65535)
		return -1;
	return (int)p;
}

static void cass2_alloc(uv_handle_t *h, size_t suggested, uv_buf_t *buf) {
	(void)h;
	buf->base = malloc(suggested);
	buf->len = suggested;
}

static int be16(const uint8_t *p) { return ((int)p[0] << 8) | p[1]; }
static int32_t be32(const uint8_t *p) { return ((int32_t)p[0] << 24) | ((int32_t)p[1] << 16) | ((int32_t)p[2] << 8) | p[3]; }

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static void cass2_free_meta(cassandra_conn_t *c) {
	if (c->col_names) {
		for (int i = 0; i < c->col_count; i++)
			free(c->col_names[i]);
		free(c->col_names);
		c->col_names = NULL;
	}
	free(c->col_types);
	c->col_types = NULL;
	c->col_count = 0;
}

static void cass2_on_write_completed(uv_write_t* req, int status) {
	(void)status;
	free(req->data);
	free(req);
}

static void cass2_fail_query(cassandra_conn_t *c, int ok) {
	cass_query_ctx_t *q = c->active_query;
	if (!q)
		return;
	uv_timer_stop(&c->query_timer);
	uv_mutex_lock(&q->mutex);
	q->ok = ok;
	q->done = 1;
	uv_cond_signal(&q->cond);
	uv_mutex_unlock(&q->mutex);
	c->active_query = NULL;
	cass2_free_meta(c);
}

static void cass2_write_raw(cassandra_conn_t *c, const uint8_t *buf, size_t len) {
	uint8_t *copy = malloc(len);
	if (!copy)
		return;
	memcpy(copy, buf, len);
	uv_buf_t b = uv_buf_init((char*)copy, (unsigned int)len);
	uv_write_t *w = malloc(sizeof(*w));
	if (!w) {
		free(copy);
		return;
	}
	w->data = copy;
	uv_write(w, (uv_stream_t*)&c->handle, &b, 1, cass2_on_write_completed);
}

static void cass2_send_frame(cassandra_conn_t *c, uint8_t opcode, int16_t stream, const uint8_t *body, uint32_t body_len, int proto) {
	uint32_t total = 9 + body_len;
	uint8_t *pkt = malloc(total);
	if (!pkt)
		return;
	pkt[0] = (uint8_t)proto;
	pkt[1] = 0;
	put16(pkt + 2, (uint16_t)stream);
	pkt[4] = opcode;
	put32(pkt + 5, body_len);
	if (body_len)
		memcpy(pkt + 9, body, body_len);
	cass2_write_raw(c, pkt, total);
	free(pkt);
}

static void cass2_send_options(cassandra_conn_t *c) {
	c->options_sent = 1;
	cass2_send_frame(c, CASS_OP_OPTIONS, 0, NULL, 0, c->requested_proto);
}

static void cass2_send_startup(cassandra_conn_t *c) {
	uint8_t body[64];
	uint8_t *p = body;
	put16(p, 1); p += 2;
	put16(p, 11); p += 2; memcpy(p, "CQL_VERSION", 11); p += 11;
	put16(p, 5); p += 2; memcpy(p, "3.0.0", 5); p += 5;
	c->startup_sent = 1;
	cass2_send_frame(c, CASS_OP_STARTUP, 0, body, (uint32_t)(p - body), c->requested_proto);
}

static void cass2_send_auth_response(cassandra_conn_t *c) {
	const char *u = c->user;
	const char *pw = c->password;
	size_t ul = strlen(u), pl = strlen(pw);
	size_t tok_len = 2 + ul + pl;
	uint8_t *token = malloc(tok_len);
	uint8_t *p = token;
	*p++ = 0;
	memcpy(p, u, ul); p += ul;
	*p++ = 0;
	memcpy(p, pw, pl);

	uint8_t *body = malloc(4 + tok_len);
	put32(body, (uint32_t)tok_len);
	memcpy(body + 4, token, tok_len);
	c->auth_sent = 1;
	cass2_send_frame(c, CASS_OP_AUTH_RESPONSE, 0, body, (uint32_t)(4 + tok_len), c->ready_proto ? c->ready_proto : c->requested_proto);
	free(token);
	free(body);
}

static void cass2_on_connect_timeout(uv_timer_t *t) {
	cassandra_conn_t *c = t->data;
	c->state = CASS_STATE_FAILED;
	uv_close((uv_handle_t*)&c->handle, NULL);
}

static void cass2_on_query_timeout(uv_timer_t *t) {
	cassandra_conn_t *c = t->data;
	if (c->active_query)
		c->active_query->timed_out = 1;
	cass2_fail_query(c, 0);
}

static int cass2_skip_string(const uint8_t **pp, const uint8_t *end) {
	if (*pp + 2 > end) return 0;
	int l = be16(*pp); *pp += 2;
	if (*pp + l > end) return 0;
	*pp += l;
	return 1;
}

static int cass2_parse_type(const uint8_t **pp, const uint8_t *end, uint16_t *out) {
	if (*pp + 2 > end) return 0;
	uint16_t id = (uint16_t)be16(*pp); *pp += 2;
	*out = id;
	if (id == 0x0020) return cass2_parse_type(pp, end, out);
	if (id == 0x0022) return cass2_parse_type(pp, end, out);
	if (id == 0x0021) {
		uint16_t t1 = 0, t2 = 0;
		if (!cass2_parse_type(pp, end, &t1))
			return 0;
		if (!cass2_parse_type(pp, end, &t2))
			return 0;
		return 1;
	}
	if (id == 0x0030) {
		if (*pp + 2 > end) return 0;
		int n = be16(*pp); *pp += 2;
		for (int i = 0; i < n; i++) {
			if (!cass2_skip_string(pp, end)) return 0;
			uint16_t t = 0;
			if (!cass2_parse_type(pp, end, &t)) return 0;
		}
	}
	return 1;
}

static int cass2_parse_rows_meta(cassandra_conn_t *c, const uint8_t **pp, const uint8_t *end) {
	if (*pp + 8 > end) return 0;
	int32_t flags = be32(*pp); *pp += 4;
	int32_t col_count = be32(*pp); *pp += 4;
	if (col_count < 0 || col_count > 4096) return 0;

	c->col_count = col_count;
	c->col_names = calloc((size_t)col_count, sizeof(char*));
	c->col_types = calloc((size_t)col_count, sizeof(uint16_t));
	if (!c->col_names || !c->col_types) return 0;

	if (flags & 0x0002) {
		if (*pp + 4 > end) return 0;
		int32_t pl = be32(*pp); *pp += 4;
		if (pl < 0 || *pp + pl > end) return 0;
		*pp += pl;
	}

	int global = (flags & 0x0001) ? 1 : 0;
	if (global) {
		if (!cass2_skip_string(pp, end)) return 0;
		if (!cass2_skip_string(pp, end)) return 0;
	}

	for (int i = 0; i < col_count; i++) {
		if (!global) {
			if (!cass2_skip_string(pp, end)) return 0;
			if (!cass2_skip_string(pp, end)) return 0;
		}
		if (*pp + 2 > end) return 0;
		int nlen = be16(*pp); *pp += 2;
		if (nlen < 0 || *pp + nlen > end) return 0;
		c->col_names[i] = strndup((const char*)*pp, (size_t)nlen);
		*pp += nlen;
		if (!cass2_parse_type(pp, end, &c->col_types[i])) return 0;
	}
	return 1;
}

static void cass2_emit_rows(cassandra_conn_t *c, const uint8_t *p, const uint8_t *end) {
	cass_query_ctx_t *q = c->active_query;
	if (!q || !q->cb) return;
	if (p + 4 > end) return;
	int32_t rows = be32(p); p += 4;
	if (rows < 0) return;

	for (int r = 0; r < rows; r++) {
		uint8_t *fields[c->col_count];
		uint64_t lengths[c->col_count];
		for (int i = 0; i < c->col_count; i++) {
			if (p + 4 > end) return;
			int32_t l = be32(p); p += 4;
			if (l < 0) {
				fields[i] = NULL;
				lengths[i] = 0;
				continue;
			}
			if (p + l > end) return;
			fields[i] = (uint8_t*)p;
			lengths[i] = (uint64_t)l;
			p += l;
		}

		cassandra_row_t row = {
			.fields = fields,
			.lengths = lengths,
			.column_count = c->col_count,
			.row_no = q->row_no++,
			.column_names = c->col_names,
			.column_types = c->col_types
		};
		q->cb(&row, q->user_data);
	}
}

static void cass2_handle_frame(cassandra_conn_t *c, uint8_t version, uint8_t opcode, const uint8_t *body, uint32_t len) {
	(void)version;
	const uint8_t *p = body;
	const uint8_t *end = body + len;

	if (opcode == CASS_OP_ERROR) {
		if (c->requested_proto == 5)
			c->requested_proto = 4;
		c->state = CASS_STATE_FAILED;
		cass2_fail_query(c, 0);
		return;
	}

	if (c->state != CASS_STATE_READY) {
		if (opcode == CASS_OP_SUPPORTED && c->options_sent) {
			c->ready_proto = c->requested_proto;
			cass2_send_startup(c);
			return;
		}
		if (opcode == CASS_OP_AUTHENTICATE) {
			cass2_send_auth_response(c);
			return;
		}
		if (opcode == CASS_OP_AUTH_SUCCESS || opcode == CASS_OP_READY) {
			c->state = CASS_STATE_READY;
			return;
		}
		return;
	}

	if (!c->active_query)
		return;

	if (opcode != CASS_OP_RESULT) {
		cass2_fail_query(c, 0);
		return;
	}

	if (p + 4 > end) {
		cass2_fail_query(c, 0);
		return;
	}
	int32_t kind = be32(p); p += 4;

	if (kind == CASS_RESULT_VOID) {
		cass2_fail_query(c, 1);
		return;
	}
	if (kind != CASS_RESULT_ROWS) {
		cass2_fail_query(c, 0);
		return;
	}

	cass2_free_meta(c);
	if (!cass2_parse_rows_meta(c, &p, end)) {
		cass2_fail_query(c, 0);
		return;
	}
	cass2_emit_rows(c, p, end);
	cass2_fail_query(c, 1);
}

static void cass2_on_read(uv_stream_t *s, ssize_t nread, const uv_buf_t *buf) {
	cassandra_conn_t *c = s->data;
	if (nread < 0) {
		c->state = CASS_STATE_FAILED;
		if (c->active_query)
			cass2_fail_query(c, 0);
		uv_read_stop(s);
		if (!uv_is_closing((uv_handle_t*)s))
			uv_close((uv_handle_t*)s, NULL);
		if (buf->base) free(buf->base);
		return;
	}
	if (nread == 0) {
		if (buf->base) free(buf->base);
		return;
	}

	if (c->rbuf_len + (size_t)nread > c->rbuf_cap) {
		size_t new_cap = c->rbuf_cap ? c->rbuf_cap * 2 : 4096;
		while (new_cap < c->rbuf_len + (size_t)nread) new_cap *= 2;
		uint8_t *nb = realloc(c->rbuf, new_cap);
		if (!nb) {
			c->state = CASS_STATE_FAILED;
			if (buf->base) free(buf->base);
			return;
		}
		c->rbuf = nb;
		c->rbuf_cap = new_cap;
	}
	memcpy(c->rbuf + c->rbuf_len, buf->base, (size_t)nread);
	c->rbuf_len += (size_t)nread;
	free(buf->base);

	size_t off = 0;
	while (c->rbuf_len - off >= 9) {
		const uint8_t *h = c->rbuf + off;
		uint8_t version = h[0];
		uint8_t opcode = h[4];
		uint32_t len = (uint32_t)be32(h + 5);
		if (len > CASS_MAX_FRAME) {
			c->state = CASS_STATE_FAILED;
			return;
		}
		if (c->rbuf_len - off < 9 + len)
			break;
		cass2_handle_frame(c, version, opcode, h + 9, len);
		off += 9 + len;
	}
	if (off) {
		memmove(c->rbuf, c->rbuf + off, c->rbuf_len - off);
		c->rbuf_len -= off;
	}
}

static void cass2_on_connect(uv_connect_t *req, int status) {
	cassandra_conn_t *c = req->data;
	free(req);
	uv_timer_stop(&c->connect_timer);
	if (status < 0) {
		c->state = CASS_STATE_FAILED;
		return;
	}
	c->state = CASS_STATE_HANDSHAKE;
	uv_read_start((uv_stream_t*)&c->handle, cass2_alloc, cass2_on_read);
	cass2_send_options(c);
}

int cassandra2_start_connect(cassandra_conn_t **pconn, context_arg *carg) {
	if (!pconn || !carg) return 0;
	if (!carg->loop)
		carg->loop = get_threaded_loop_t_or_default(carg->threaded_loop_name);

	if (!*pconn) {
		cassandra_conn_t *c = cass2_conn_new(carg->loop, 5);
		if (!c) return 0;
		*pconn = c;
	}

	cassandra_conn_t *c = *pconn;
	c->carg = carg;
	strlcpy(c->user, carg->user, sizeof(c->user));
	strlcpy(c->password, carg->password, sizeof(c->password));
	if (c->state == CASS_STATE_READY)
		return 1;
	if (c->state == CASS_STATE_HANDSHAKE || c->state == CASS_STATE_CONNECTING)
		return 1;
	if (c->state == CASS_STATE_FAILED) {
		int next_proto = (c->requested_proto == 5) ? 4 : c->requested_proto;
		cassandra_conn_t *n = cass2_conn_new(carg->loop, next_proto);
		if (!n) return 0;
		n->carg = carg;
		strlcpy(n->user, carg->user, sizeof(n->user));
		strlcpy(n->password, carg->password, sizeof(n->password));
		*pconn = c = n;
	}

	int port = cass2_port_from_carg(carg);
	if (port < 0) {
		c->state = CASS_STATE_FAILED;
		return 0;
	}
	struct sockaddr_in dest;
	uv_ip4_addr(carg->host, port, &dest);
	uv_connect_t *cr = malloc(sizeof(*cr));
	if (!cr) return 0;
	cr->data = c;
	c->state = CASS_STATE_CONNECTING;
	if (c->requested_proto != 4)
		c->requested_proto = 5;
	c->ready_proto = 0;
	c->startup_sent = c->auth_sent = c->options_sent = 0;
	uv_timer_start(&c->connect_timer, cass2_on_connect_timeout, CASS_CONNECT_TIMEOUT_MS, 0);
	if (uv_tcp_connect(cr, &c->handle, (const struct sockaddr*)&dest, cass2_on_connect) < 0) {
		free(cr);
		c->state = CASS_STATE_FAILED;
		return 0;
	}

	return 1;
}

static void cass2_send_query(cassandra_conn_t *c, const char *q) {
	size_t ql = strlen(q);
	uint8_t *body = malloc(4 + ql + 2 + 1);
	put32(body, (uint32_t)ql);
	memcpy(body + 4, q, ql);
	put16(body + 4 + ql, 0x0001);
	body[4 + ql + 2] = 0x00;
	int16_t stream = (int16_t)(c->stream_id++ & 0x7fff);
	c->current_stream = stream;
	cass2_send_frame(c, CASS_OP_QUERY, stream, body, (uint32_t)(4 + ql + 3), c->ready_proto ? c->ready_proto : 4);
	free(body);
}

void cassandra2_await_query(cassandra_conn_t *conn, const char *cql, cassandra_row_cb cb, void *user_data) {
	if (!conn || conn->state != CASS_STATE_READY || !cql)
		return;

	cass_query_ctx_t q;
	memset(&q, 0, sizeof(q));
	uv_mutex_init(&q.mutex);
	uv_cond_init(&q.cond);
	q.conn = conn;
	q.cb = cb;
	q.user_data = user_data;
	conn->active_query = &q;

	uv_timer_start(&conn->query_timer, cass2_on_query_timeout, CASS_QUERY_TIMEOUT_MS, 0);
	cass2_send_query(conn, cql);

	uv_mutex_lock(&q.mutex);
	while (!q.done)
		uv_cond_wait(&q.cond, &q.mutex);
	uv_mutex_unlock(&q.mutex);

	uv_mutex_destroy(&q.mutex);
	uv_cond_destroy(&q.cond);
}

int cassandra2_use_keyspace(cassandra_conn_t *conn, const char *keyspace) {
	if (!conn || !keyspace || !keyspace[0])
		return 0;
	char query[512];
	snprintf(query, sizeof(query), "USE \"%s\"", keyspace);

	cass_query_ctx_t q;
	memset(&q, 0, sizeof(q));
	uv_mutex_init(&q.mutex);
	uv_cond_init(&q.cond);
	q.conn = conn;
	conn->active_query = &q;
	uv_timer_start(&conn->query_timer, cass2_on_query_timeout, CASS_QUERY_TIMEOUT_MS, 0);
	cass2_send_query(conn, query);
	uv_mutex_lock(&q.mutex);
	while (!q.done)
		uv_cond_wait(&q.cond, &q.mutex);
	uv_mutex_unlock(&q.mutex);
	int ok = q.ok && !q.timed_out;
	uv_mutex_destroy(&q.mutex);
	uv_cond_destroy(&q.cond);
	return ok;
}

int cassandra2_is_ready(cassandra_conn_t *conn) { return conn && conn->state == CASS_STATE_READY; }
int cassandra2_is_failed(cassandra_conn_t *conn) { return conn && conn->state == CASS_STATE_FAILED; }
