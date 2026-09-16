#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include <librdkafka/rdkafka.h>
#include <uv.h>

#include "events/kafka_consumer.h"
#include "events/context_arg.h"
#include "events/metrics.h"
#include "common/logs.h"
#include "parsers/multiparser.h"
#include "main.h"

extern aconf *ac;

#define KAFKA_CONSUMER_POLL_MS 100
#define KAFKA_CONSUMER_BATCH 64
#define KAFKA_CONSUMER_ADDR_SIZE 288

typedef struct kafka_consumer_state {
	rd_kafka_t *rk;
	uv_timer_t *timer;
	char *topic;
	int closing;
} kafka_consumer_state;

int kafka_consumer_split_query(const char *query_url, char **topic, char **opts)
{
	const char *qmark;
	size_t tlen;
	char *tcopy;

	if (!query_url || !query_url[0] || !topic)
		return -1;

	qmark = strchr(query_url, '?');
	tlen = qmark ? (size_t)(qmark - query_url) : strlen(query_url);
	if (!tlen)
		return -1;

	tcopy = malloc(tlen + 1);
	if (!tcopy)
		return -1;
	memcpy(tcopy, query_url, tlen);
	tcopy[tlen] = '\0';
	*topic = tcopy;

	if (opts) {
		if (qmark && qmark[1])
			*opts = strdup(qmark + 1);
		else
			*opts = NULL;
	}
	return 0;
}

static int kafka_consumer_apply_opts(rd_kafka_conf_t *conf, const char *opts, context_arg *carg)
{
	char *copy;
	char *token;
	char *saveptr = NULL;
	char errstr[512];

	if (!conf || !opts || !opts[0])
		return 0;

	copy = strdup(opts);
	if (!copy)
		return -1;
	if (copy[0] == '?')
		memmove(copy, copy + 1, strlen(copy));

	token = strtok_r(copy, "&", &saveptr);
	while (token) {
		char *eq = strchr(token, '=');
		if (eq) {
			*eq = '\0';
			if (token[0] && eq[1]) {
				if (rd_kafka_conf_set(conf, token, eq + 1, errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK)
					carglog(carg, L_ERROR, "kafka_consumer: librdkafka conf '%s' rejected: %s\n",
						token, errstr);
			}
		}
		token = strtok_r(NULL, "&", &saveptr);
	}
	free(copy);
	return 0;
}

static void kafka_consumer_feed(context_arg *carg, const char *payload, size_t len)
{
	char *buf;
	size_t feed_len;

	if (!payload || !len)
		return;

	carg->read_counter++;
	carg->read_bytes_counter += len;

	if (payload[len - 1] == '\n') {
		alligator_multiparser((char *)payload, len, carg->parser_handler, NULL, carg);
		return;
	}

	feed_len = len + 1;
	buf = malloc(feed_len);
	if (!buf) {
		alligator_multiparser((char *)payload, len, carg->parser_handler, NULL, carg);
		return;
	}
	memcpy(buf, payload, len);
	buf[len] = '\n';
	alligator_multiparser(buf, feed_len, carg->parser_handler, NULL, carg);
	free(buf);
}

static void kafka_consumer_poll_cb(uv_timer_t *timer)
{
	context_arg *carg = timer->data;
	kafka_consumer_state *st;
	int i;

	if (!carg)
		return;
	st = carg->data;
	if (!st || st->closing || !st->rk)
		return;

	for (i = 0; i < KAFKA_CONSUMER_BATCH; i++) {
		rd_kafka_message_t *rkmessage;

		rkmessage = rd_kafka_consumer_poll(st->rk, 0);
		if (!rkmessage)
			break;

		if (rkmessage->err) {
			if (rkmessage->err != RD_KAFKA_RESP_ERR__PARTITION_EOF)
				carglog(carg, L_ERROR, "kafka_consumer: consume error: %s\n",
					rd_kafka_message_errstr(rkmessage));
			rd_kafka_message_destroy(rkmessage);
			continue;
		}

		if (rkmessage->payload && rkmessage->len > 0)
			kafka_consumer_feed(carg, rkmessage->payload, (size_t)rkmessage->len);

		rd_kafka_message_destroy(rkmessage);
	}
}

static void kafka_consumer_timer_close_cb(uv_handle_t *handle)
{
	free(handle);
}

static void kafka_consumer_state_destroy(kafka_consumer_state *st)
{
	if (!st)
		return;

	if (st->rk) {
		rd_kafka_consumer_close(st->rk);
		rd_kafka_destroy(st->rk);
		st->rk = NULL;
	}
	free(st->topic);
	free(st);
}

char *kafka_consumer_handler(context_arg *carg)
{
	kafka_consumer_state *st;
	rd_kafka_conf_t *conf;
	rd_kafka_topic_partition_list_t *topics;
	rd_kafka_resp_err_t err;
	char errstr[512];
	char brokers[KAFKA_CONSUMER_ADDR_SIZE];
	char *topic = NULL;
	char *opts = NULL;
	const char *port;

	if (!carg)
		return NULL;

	if (kafka_consumer_split_query(carg->query_url, &topic, &opts) != 0) {
		carglog(carg, L_ERROR,
			"kafka_consumer: URL must include a topic path (kafka://host:9092/topic)\n");
		return NULL;
	}

	st = calloc(1, sizeof(*st));
	if (!st) {
		free(topic);
		free(opts);
		return NULL;
	}
	st->topic = topic;

	carg->loop = get_threaded_loop_t_or_default(carg->threaded_loop_name);

	port = carg->port[0] ? carg->port : "9092";
	snprintf(brokers, sizeof(brokers), "%s:%s", carg->host, port);

	conf = rd_kafka_conf_new();
	if (!conf) {
		kafka_consumer_state_destroy(st);
		free(opts);
		return NULL;
	}

	if (rd_kafka_conf_set(conf, "bootstrap.servers", brokers, errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
		carglog(carg, L_ERROR, "kafka_consumer: bootstrap.servers=%s: %s\n", brokers, errstr);
		rd_kafka_conf_destroy(conf);
		kafka_consumer_state_destroy(st);
		free(opts);
		return NULL;
	}

	rd_kafka_conf_set(conf, "client.id", "alligator-kafka-consumer", errstr, sizeof(errstr));
	rd_kafka_conf_set(conf, "group.id", "alligator", errstr, sizeof(errstr));
	rd_kafka_conf_set(conf, "auto.offset.reset", "latest", errstr, sizeof(errstr));
	rd_kafka_conf_set(conf, "enable.auto.commit", "true", errstr, sizeof(errstr));
	rd_kafka_conf_set(conf, "allow.auto.create.topics", "false", errstr, sizeof(errstr));
	rd_kafka_conf_set(conf, "log.connection.close", "false", errstr, sizeof(errstr));

	if (carg->user[0]) {
		rd_kafka_conf_set(conf, "security.protocol", "SASL_PLAINTEXT", errstr, sizeof(errstr));
		rd_kafka_conf_set(conf, "sasl.mechanism", "PLAIN", errstr, sizeof(errstr));
		rd_kafka_conf_set(conf, "sasl.username", carg->user, errstr, sizeof(errstr));
		if (carg->password[0])
			rd_kafka_conf_set(conf, "sasl.password", carg->password, errstr, sizeof(errstr));
	}

	kafka_consumer_apply_opts(conf, opts, carg);
	free(opts);

	st->rk = rd_kafka_new(RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));
	if (!st->rk) {
		carglog(carg, L_ERROR, "kafka_consumer: rd_kafka_new failed: %s\n", errstr);
		rd_kafka_conf_destroy(conf);
		kafka_consumer_state_destroy(st);
		return NULL;
	}

	err = rd_kafka_poll_set_consumer(st->rk);
	if (err) {
		carglog(carg, L_ERROR, "kafka_consumer: poll_set_consumer: %s\n", rd_kafka_err2str(err));
		kafka_consumer_state_destroy(st);
		return NULL;
	}

	topics = rd_kafka_topic_partition_list_new(1);
	rd_kafka_topic_partition_list_add(topics, st->topic, RD_KAFKA_PARTITION_UA);
	err = rd_kafka_subscribe(st->rk, topics);
	rd_kafka_topic_partition_list_destroy(topics);
	if (err) {
		carglog(carg, L_ERROR, "kafka_consumer: subscribe topic '%s': %s\n",
			st->topic, rd_kafka_err2str(err));
		kafka_consumer_state_destroy(st);
		return NULL;
	}

	st->timer = calloc(1, sizeof(uv_timer_t));
	if (!st->timer) {
		kafka_consumer_state_destroy(st);
		return NULL;
	}
	st->timer->data = carg;
	uv_timer_init(carg->loop, st->timer);
	uv_timer_start(st->timer, kafka_consumer_poll_cb, 0, KAFKA_CONSUMER_POLL_MS);

	carg->data = st;
	carg->conn_counter++;

	carglog(carg, L_INFO, "kafka_consumer: subscribed to topic '%s' on %s\n", st->topic, brokers);
	aggregator_events_metric_add(carg, carg, NULL, "kafka", "aggregator", carg->host);

	return "kafka";
}

void kafka_consumer_handler_del(context_arg *carg)
{
	kafka_consumer_state *st;

	if (!carg)
		return;
	st = carg->data;
	if (!st)
		return;

	st->closing = 1;

	if (st->timer) {
		uv_timer_stop(st->timer);
		if (!uv_is_closing((uv_handle_t *)st->timer))
			uv_close((uv_handle_t *)st->timer, kafka_consumer_timer_close_cb);
		st->timer = NULL;
	}

	kafka_consumer_state_destroy(st);
	carg->data = NULL;
	carg->close_counter++;
}
