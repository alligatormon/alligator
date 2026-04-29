#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "common/selector.h"
#include "parsers/mongodb_wire_bson.h"
#include "parsers/mongodb_wire_client.h"
#include "parsers/bson.h"

#define MONGO_OP_MSG 2013
#define MONGO_MAX_MSG (64 * 1024 * 1024)

struct mongodb_wire_client {
	int fd;
	int32_t req_id;
	char host[256];
	int port;
	char db[128];
	char user[128];
	char pass[128];
	char auth_db[128];
};

static void mongo_set_err(char *err, size_t errsz, const char *msg)
{
	if (err && errsz)
		strlcpy(err, msg, errsz);
}

static int mongo_read_full(int fd, void *buf, size_t len)
{
	size_t off = 0;
	while (off < len) {
		ssize_t r = read(fd, (uint8_t*)buf + off, len - off);
		if (r <= 0)
			return 0;
		off += (size_t)r;
	}
	return 1;
}

static int mongo_write_full(int fd, const void *buf, size_t len)
{
	size_t off = 0;
	while (off < len) {
		ssize_t w = write(fd, (const uint8_t*)buf + off, len - off);
		if (w <= 0)
			return 0;
		off += (size_t)w;
	}
	return 1;
}

static int mongo_send_cmd(struct mongodb_wire_client *client, bson_buf_t *body, char *err, size_t errsz)
{
	uint8_t *pkt;
	uint32_t total_len;
	int32_t header[4];
	uint32_t flags = 0;
	uint8_t kind = 0;

	bson_finish(body);
	total_len = 16 + 4 + 1 + (uint32_t)body->size;
	pkt = malloc(total_len);
	if (!pkt) {
		mongo_set_err(err, errsz, "oom while building mongodb packet");
		return 0;
	}

	header[0] = (int32_t)total_len;
	header[1] = ++client->req_id;
	header[2] = 0;
	header[3] = MONGO_OP_MSG;
	memcpy(pkt, header, 16);
	memcpy(pkt + 16, &flags, 4);
	memcpy(pkt + 20, &kind, 1);
	memcpy(pkt + 21, body->data, body->size);

	if (!mongo_write_full(client->fd, pkt, total_len)) {
		free(pkt);
		mongo_set_err(err, errsz, "socket write failed");
		return 0;
	}
	free(pkt);
	return 1;
}

static int mongo_recv_reply(struct mongodb_wire_client *client, uint8_t **msg, uint32_t *msg_len, char *err, size_t errsz)
{
	int32_t len = 0;
	uint8_t *buf;
	if (!mongo_read_full(client->fd, &len, 4)) {
		mongo_set_err(err, errsz, "socket read failed");
		return 0;
	}
	if (len < 21 || len > MONGO_MAX_MSG) {
		mongo_set_err(err, errsz, "invalid mongodb message length");
		return 0;
	}
	buf = malloc((size_t)len);
	if (!buf) {
		mongo_set_err(err, errsz, "oom while reading mongodb packet");
		return 0;
	}
	memcpy(buf, &len, 4);
	if (!mongo_read_full(client->fd, buf + 4, (size_t)len - 4)) {
		free(buf);
		mongo_set_err(err, errsz, "socket read payload failed");
		return 0;
	}
	*msg = buf;
	*msg_len = (uint32_t)len;
	return 1;
}

static int mongo_decode_document_from_msg(const uint8_t *msg, uint32_t msg_len, json_t **doc_out)
{
	if (!msg || msg_len <= 21 || !doc_out)
		return 0;
	return bson_to_json_document(msg + 21, msg_len - 21, doc_out, 0) != 0;
}

static int mongo_reply_ok(json_t *doc, char *err, size_t errsz)
{
	json_t *okv = json_object_get(doc, "ok");
	if (okv && (json_is_real(okv) || json_is_integer(okv))) {
		double ok = json_number_value(okv);
		if (ok == 1.0)
			return 1;
	}

	json_t *errmsg = json_object_get(doc, "errmsg");
	if (errmsg && json_is_string(errmsg))
		mongo_set_err(err, errsz, json_string_value(errmsg));
	else
		mongo_set_err(err, errsz, "mongodb command failed");
	return 0;
}

static int mongo_parse_uri(const char *uri, struct mongodb_wire_client *c, char *err, size_t errsz)
{
	const char *p;
	const char *at;
	const char *slash;
	const char *q;
	char hostport[256] = {0};

	if (!uri || strncmp(uri, "mongodb://", 10)) {
		mongo_set_err(err, errsz, "uri must start with mongodb://");
		return 0;
	}
	p = uri + 10;
	at = strchr(p, '@');
	if (!at) {
		mongo_set_err(err, errsz, "uri must contain user:pass@host");
		return 0;
	}
	slash = strchr(at + 1, '/');
	if (!slash) {
		mongo_set_err(err, errsz, "uri must contain /database");
		return 0;
	}
	q = strchr(slash + 1, '?');

	{
		const char *colon = strchr(p, ':');
		if (!colon || colon > at) {
			mongo_set_err(err, errsz, "uri must contain user:pass");
			return 0;
		}
		strlcpy(c->user, "", sizeof(c->user));
		strlcpy(c->pass, "", sizeof(c->pass));
		if ((size_t)(colon - p) >= sizeof(c->user) || (size_t)(at - colon - 1) >= sizeof(c->pass)) {
			mongo_set_err(err, errsz, "uri credentials are too long");
			return 0;
		}
		memcpy(c->user, p, (size_t)(colon - p));
		c->user[colon - p] = '\0';
		memcpy(c->pass, colon + 1, (size_t)(at - colon - 1));
		c->pass[at - colon - 1] = '\0';
	}

	{
		size_t hp_len = (size_t)(slash - (at + 1));
		char *colon;
		if (!hp_len || hp_len >= sizeof(hostport)) {
			mongo_set_err(err, errsz, "invalid host in uri");
			return 0;
		}
		memcpy(hostport, at + 1, hp_len);
		hostport[hp_len] = '\0';
		colon = strrchr(hostport, ':');
		if (colon) {
			*colon = '\0';
			c->port = atoi(colon + 1);
		}
		else {
			c->port = 27017;
		}
		if (c->port <= 0 || c->port > 65535 || !hostport[0]) {
			mongo_set_err(err, errsz, "invalid host:port in uri");
			return 0;
		}
		strlcpy(c->host, hostport, sizeof(c->host));
	}

	{
		size_t db_len = q ? (size_t)(q - (slash + 1)) : strlen(slash + 1);
		if (!db_len || db_len >= sizeof(c->db)) {
			mongo_set_err(err, errsz, "invalid database in uri");
			return 0;
		}
		memcpy(c->db, slash + 1, db_len);
		c->db[db_len] = '\0';
	}

	strlcpy(c->auth_db, c->db, sizeof(c->auth_db));
	if (q) {
		const char *as = strstr(q + 1, "authSource=");
		if (as) {
			const char *v = as + strlen("authSource=");
			const char *end = strchr(v, '&');
			size_t len = end ? (size_t)(end - v) : strlen(v);
			if (len > 0 && len < sizeof(c->auth_db)) {
				memcpy(c->auth_db, v, len);
				c->auth_db[len] = '\0';
			}
		}
	}
	return 1;
}

static int mongo_connect_socket(struct mongodb_wire_client *client, char *err, size_t errsz)
{
	struct addrinfo hints;
	struct addrinfo *res = NULL;
	struct addrinfo *it;
	char portbuf[16];
	int fd = -1;
	int ok = 0;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	snprintf(portbuf, sizeof(portbuf), "%d", client->port);

	if (getaddrinfo(client->host, portbuf, &hints, &res) != 0) {
		mongo_set_err(err, errsz, "getaddrinfo failed");
		return 0;
	}

	for (it = res; it; it = it->ai_next) {
		fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
		if (fd < 0)
			continue;
		if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
			ok = 1;
			break;
		}
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);

	if (!ok || fd < 0) {
		mongo_set_err(err, errsz, "tcp connect failed");
		return 0;
	}
	client->fd = fd;
	return 1;
}

static int mongo_base64_decode(const char *input, uint8_t *out, size_t out_cap, size_t *out_len)
{
	int n;
	size_t in_len = strlen(input);
	if (!in_len)
		return 0;
	if (out_cap < (in_len * 3) / 4 + 4)
		return 0;
	n = EVP_DecodeBlock(out, (const unsigned char*)input, (int)in_len);
	if (n < 0)
		return 0;
	while (in_len > 0 && input[in_len - 1] == '=') {
		n--;
		in_len--;
	}
	*out_len = (size_t)n;
	return 1;
}

static void mongo_hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t out[32])
{
	unsigned int len = 32;
	HMAC(EVP_sha256(), key, (int)key_len, data, data_len, out, &len);
}

static int mongo_auth_scram_sha256(struct mongodb_wire_client *client, char *err, size_t errsz)
{
	uint8_t *msg = NULL;
	uint32_t msg_len = 0;
	json_t *doc = NULL;
	json_t *payload = NULL;
	bson_buf_t b;
	char client_first_bare[256];
	char client_first[320];
	char server_first[512];
	char nonce[80] = "alligatornonce123456789";
	char rbuf[256] = {0};
	char sbuf[256] = {0};
	int iterations = 0;
	int32_t conversation_id = 0;
	const char *sp;
	const char *rp;
	const char *ip;

	snprintf(client_first_bare, sizeof(client_first_bare), "n=%s,r=%s", client->user, nonce);
	snprintf(client_first, sizeof(client_first), "n,,%s", client_first_bare);

	bson_init(&b);
	bson_append_int32(&b, "saslStart", 1);
	bson_append_utf8(&b, "mechanism", "SCRAM-SHA-256");
	bson_append_binary(&b, "payload", (const uint8_t*)client_first, (uint32_t)strlen(client_first));
	bson_append_utf8(&b, "$db", client->auth_db);
	if (!mongo_send_cmd(client, &b, err, errsz)) {
		free(b.data);
		return 0;
	}
	free(b.data);

	if (!mongo_recv_reply(client, &msg, &msg_len, err, errsz))
		return 0;
	if (!mongo_decode_document_from_msg(msg, msg_len, &doc)) {
		free(msg);
		mongo_set_err(err, errsz, "invalid saslStart reply");
		return 0;
	}
	free(msg);
	if (!mongo_reply_ok(doc, err, errsz)) {
		json_decref(doc);
		return 0;
	}
	payload = json_object_get(doc, "payload");
	if (!payload || !json_is_string(payload)) {
		json_decref(doc);
		mongo_set_err(err, errsz, "missing saslStart payload");
		return 0;
	}
	strlcpy(server_first, json_string_value(payload), sizeof(server_first));
	{
		json_t *cid = json_object_get(doc, "conversationId");
		if (!cid || !json_is_integer(cid)) {
			json_decref(doc);
			mongo_set_err(err, errsz, "missing conversationId");
			return 0;
		}
		conversation_id = (int32_t)json_integer_value(cid);
	}
	json_decref(doc);

	rp = strstr(server_first, "r=");
	sp = strstr(server_first, "s=");
	ip = strstr(server_first, "i=");
	if (!rp || !sp || !ip) {
		mongo_set_err(err, errsz, "invalid scram server-first message");
		return 0;
	}
	sscanf(rp, "r=%255[^,]", rbuf);
	sscanf(sp, "s=%255[^,]", sbuf);
	sscanf(ip, "i=%d", &iterations);
	if (!rbuf[0] || !sbuf[0] || iterations <= 0) {
		mongo_set_err(err, errsz, "invalid scram server-first payload");
		return 0;
	}

	{
		uint8_t salt[256];
		size_t salt_len = 0;
		uint8_t salted_password[32];
		uint8_t client_key[32];
		uint8_t stored_key[32];
		uint8_t client_sig[32];
		uint8_t proof[32];
		char client_final_no_proof[320];
		char auth_msg[1200];
		char b64_proof[256];
		char final_ascii[640];

		if (!mongo_base64_decode(sbuf, salt, sizeof(salt), &salt_len)) {
			mongo_set_err(err, errsz, "failed to decode scram salt");
			return 0;
		}

		PKCS5_PBKDF2_HMAC(client->pass, (int)strlen(client->pass), salt, (int)salt_len, iterations, EVP_sha256(), 32, salted_password);
		mongo_hmac_sha256(salted_password, sizeof(salted_password), (const uint8_t*)"Client Key", 10, client_key);
		SHA256(client_key, sizeof(client_key), stored_key);

		snprintf(client_final_no_proof, sizeof(client_final_no_proof), "c=biws,r=%s", rbuf);
		snprintf(auth_msg, sizeof(auth_msg), "%s,%s,%s", client_first_bare, server_first, client_final_no_proof);
		mongo_hmac_sha256(stored_key, sizeof(stored_key), (const uint8_t*)auth_msg, strlen(auth_msg), client_sig);
		for (int i = 0; i < 32; ++i)
			proof[i] = client_key[i] ^ client_sig[i];
		EVP_EncodeBlock((unsigned char*)b64_proof, proof, 32);
		snprintf(final_ascii, sizeof(final_ascii), "c=biws,r=%s,p=%s", rbuf, b64_proof);

		bson_init(&b);
		bson_append_int32(&b, "saslContinue", 1);
		bson_append_int32(&b, "conversationId", conversation_id);
		bson_append_binary(&b, "payload", (const uint8_t*)final_ascii, (uint32_t)strlen(final_ascii));
		bson_append_utf8(&b, "$db", client->auth_db);
		if (!mongo_send_cmd(client, &b, err, errsz)) {
			free(b.data);
			return 0;
		}
		free(b.data);
	}

	if (!mongo_recv_reply(client, &msg, &msg_len, err, errsz))
		return 0;
	if (!mongo_decode_document_from_msg(msg, msg_len, &doc)) {
		free(msg);
		mongo_set_err(err, errsz, "invalid saslContinue reply");
		return 0;
	}
	free(msg);
	if (!mongo_reply_ok(doc, err, errsz)) {
		json_decref(doc);
		return 0;
	}

	{
		json_t *done = json_object_get(doc, "done");
		json_t *cid = json_object_get(doc, "conversationId");
		int need_final = (!done || !json_is_true(done));
		if (cid && json_is_integer(cid))
			conversation_id = (int32_t)json_integer_value(cid);

		json_decref(doc);
		if (!need_final)
			return 1;
	}

	bson_init(&b);
	bson_append_int32(&b, "saslContinue", 1);
	bson_append_int32(&b, "conversationId", conversation_id);
	bson_append_binary(&b, "payload", (const uint8_t*)"", 0);
	bson_append_utf8(&b, "$db", client->auth_db);
	if (!mongo_send_cmd(client, &b, err, errsz)) {
		free(b.data);
		return 0;
	}
	free(b.data);

	if (!mongo_recv_reply(client, &msg, &msg_len, err, errsz))
		return 0;
	if (!mongo_decode_document_from_msg(msg, msg_len, &doc)) {
		free(msg);
		mongo_set_err(err, errsz, "invalid final saslContinue reply");
		return 0;
	}
	free(msg);
	if (!mongo_reply_ok(doc, err, errsz)) {
		json_decref(doc);
		return 0;
	}
	json_decref(doc);
	return 1;
}

int mongodb_wire_client_open(mongodb_wire_client **out, const char *uri, char *err, size_t errsz)
{
	struct mongodb_wire_client *client;
	if (!out)
		return 0;

	client = calloc(1, sizeof(*client));
	if (!client) {
		mongo_set_err(err, errsz, "oom while creating mongodb client");
		return 0;
	}
	client->fd = -1;
	client->req_id = 1;
	if (!mongo_parse_uri(uri, client, err, errsz) ||
	    !mongo_connect_socket(client, err, errsz) ||
	    !mongo_auth_scram_sha256(client, err, errsz)) {
		if (client->fd >= 0)
			close(client->fd);
		free(client);
		return 0;
	}
	*out = client;
	return 1;
}

void mongodb_wire_client_close(mongodb_wire_client *client)
{
	if (!client)
		return;
	if (client->fd >= 0)
		close(client->fd);
	free(client);
}

static int mongo_collect_names_from_array(json_t *arr, char ***names, size_t *count)
{
	size_t i;
	size_t n;
	char **out;

	if (!arr || !json_is_array(arr) || !names || !count)
		return 0;
	n = json_array_size(arr);
	out = calloc(n ? n : 1, sizeof(char*));
	if (!out)
		return 0;

	for (i = 0; i < n; ++i) {
		json_t *obj = json_array_get(arr, i);
		json_t *name = obj ? json_object_get(obj, "name") : NULL;
		if (!name || !json_is_string(name))
			continue;
		out[*count] = strdup(json_string_value(name));
		if (!out[*count]) {
			mongodb_wire_free_string_list(out, *count);
			return 0;
		}
		(*count)++;
	}
	*names = out;
	return 1;
}

int mongodb_wire_list_databases(mongodb_wire_client *client, char ***names, size_t *count, char *err, size_t errsz)
{
	bson_buf_t b;
	uint8_t *msg = NULL;
	uint32_t msg_len = 0;
	json_t *doc = NULL;
	json_t *dbs = NULL;

	if (!client || !names || !count)
		return 0;
	*names = NULL;
	*count = 0;

	bson_init(&b);
	bson_append_int32(&b, "listDatabases", 1);
	bson_append_bool(&b, "nameOnly", 1);
	bson_append_utf8(&b, "$db", "admin");
	if (!mongo_send_cmd(client, &b, err, errsz)) {
		free(b.data);
		return 0;
	}
	free(b.data);
	if (!mongo_recv_reply(client, &msg, &msg_len, err, errsz))
		return 0;
	if (!mongo_decode_document_from_msg(msg, msg_len, &doc)) {
		free(msg);
		mongo_set_err(err, errsz, "invalid listDatabases reply");
		return 0;
	}
	free(msg);
	if (!mongo_reply_ok(doc, err, errsz)) {
		json_decref(doc);
		return 0;
	}
	dbs = json_object_get(doc, "databases");
	if (!mongo_collect_names_from_array(dbs, names, count)) {
		json_decref(doc);
		mongo_set_err(err, errsz, "failed to parse databases list");
		return 0;
	}
	json_decref(doc);
	return 1;
}

int mongodb_wire_list_collections(mongodb_wire_client *client, const char *dbname, char ***names, size_t *count, char *err, size_t errsz)
{
	bson_buf_t b;
	uint8_t *msg = NULL;
	uint32_t msg_len = 0;
	json_t *doc = NULL;
	json_t *cursor = NULL;
	json_t *batch = NULL;

	if (!client || !dbname || !names || !count)
		return 0;
	*names = NULL;
	*count = 0;

	bson_init(&b);
	bson_append_int32(&b, "listCollections", 1);
	bson_append_bool(&b, "nameOnly", 1);
	bson_append_utf8(&b, "$db", dbname);
	if (!mongo_send_cmd(client, &b, err, errsz)) {
		free(b.data);
		return 0;
	}
	free(b.data);

	if (!mongo_recv_reply(client, &msg, &msg_len, err, errsz))
		return 0;
	if (!mongo_decode_document_from_msg(msg, msg_len, &doc)) {
		free(msg);
		mongo_set_err(err, errsz, "invalid listCollections reply");
		return 0;
	}
	free(msg);
	if (!mongo_reply_ok(doc, err, errsz)) {
		json_decref(doc);
		return 0;
	}

	cursor = json_object_get(doc, "cursor");
	if (cursor && json_is_object(cursor))
		batch = json_object_get(cursor, "firstBatch");
	if (!mongo_collect_names_from_array(batch, names, count)) {
		json_decref(doc);
		mongo_set_err(err, errsz, "failed to parse collections list");
		return 0;
	}
	json_decref(doc);
	return 1;
}

int mongodb_wire_find(mongodb_wire_client *client, const char *dbname, const char *collection, const char *filter_json, mongodb_wire_doc_cb cb, void *arg, char *err, size_t errsz)
{
	bson_buf_t b;
	uint8_t *msg = NULL;
	uint32_t msg_len = 0;
	json_t *doc = NULL;
	json_t *cursor = NULL;
	json_t *batch = NULL;
	json_t *cid = NULL;
	uint8_t *raw_filter = NULL;
	uint32_t raw_filter_len = 0;
	int done = 0;
	int rc = 0;
	int64_t cursor_id = 0;

	if (!client || !dbname || !collection || !filter_json || !cb)
		return 0;
	if (!mongodb_json_to_bson_document(filter_json, &raw_filter, &raw_filter_len)) {
		mongo_set_err(err, errsz, "invalid filter json");
		return 0;
	}

	while (!done) {
		bson_init(&b);
		if (!cursor_id) {
			bson_append_utf8(&b, "find", collection);
			bson_append_document_raw(&b, "filter", raw_filter, raw_filter_len);
			bson_append_int32(&b, "batchSize", 100);
			bson_append_utf8(&b, "$db", dbname);
		}
		else {
			bson_append_int64(&b, "getMore", cursor_id);
			bson_append_utf8(&b, "collection", collection);
			bson_append_int32(&b, "batchSize", 100);
			bson_append_utf8(&b, "$db", dbname);
		}

		if (!mongo_send_cmd(client, &b, err, errsz)) {
			free(b.data);
			goto out;
		}
		free(b.data);
		if (!mongo_recv_reply(client, &msg, &msg_len, err, errsz))
			goto out;
		if (!mongo_decode_document_from_msg(msg, msg_len, &doc)) {
			free(msg);
			mongo_set_err(err, errsz, "invalid find reply");
			goto out;
		}
		free(msg);
		msg = NULL;

		if (!mongo_reply_ok(doc, err, errsz))
			goto out;

		cursor = json_object_get(doc, "cursor");
		if (!cursor || !json_is_object(cursor)) {
			mongo_set_err(err, errsz, "reply has no cursor");
			goto out;
		}
		batch = json_object_get(cursor, "firstBatch");
		if (!batch)
			batch = json_object_get(cursor, "nextBatch");
		if (batch && json_is_array(batch)) {
			size_t i;
			json_t *item;
			json_array_foreach(batch, i, item) {
				if (cb(item, arg) == 0) {
					mongo_set_err(err, errsz, "find callback failed");
					goto out;
				}
			}
		}
		cid = json_object_get(cursor, "id");
		cursor_id = (cid && json_is_integer(cid)) ? (int64_t)json_integer_value(cid) : 0;
		done = (cursor_id == 0);
		json_decref(doc);
		doc = NULL;
	}
	rc = 1;

out:
	if (raw_filter)
		free(raw_filter);
	if (msg)
		free(msg);
	if (doc)
		json_decref(doc);
	return rc;
}

void mongodb_wire_free_string_list(char **names, size_t count)
{
	if (!names)
		return;
	for (size_t i = 0; i < count; ++i)
		free(names[i]);
	free(names);
}
