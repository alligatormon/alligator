#include "common/log_kafka.h"
#include "common/logs.h"

#include <librdkafka/rdkafka.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <inttypes.h>

#define LOG_KAFKA_QUEUE_MAX_MSGS 10000
#define LOG_KAFKA_QUEUE_MAX_KB 10240
#define LOG_KAFKA_WARN_INTERVAL 60

struct log_kafka_sink {
	rd_kafka_t *rk;
	rd_kafka_topic_t *rkt;
	char *brokers;
	char *topic;
	char *query;
	char *msg_key;
	uint64_t dropped;
	uint64_t errors;
	time_t last_warn;
	uint8_t closing;
	pthread_mutex_t lock;
};

static void log_kafka_warn_throttled(log_kafka_sink *sink, const char *msg)
{
	time_t now;

	if (!sink || !msg)
		return;

	now = time(NULL);
	if (sink->last_warn && (now - sink->last_warn) < LOG_KAFKA_WARN_INTERVAL)
		return;

	sink->last_warn = now;
	fprintf(stderr, "log kafka: %s (dropped=%" PRIu64 ", errors=%" PRIu64 ")\n",
	    msg, sink->dropped, sink->errors);
}

static void log_kafka_delivery_cb(rd_kafka_t *rk, const rd_kafka_message_t *rkmessage, void *opaque)
{
	log_kafka_sink *sink = opaque;

	(void)rk;

	if (!sink || !rkmessage || rkmessage->err == RD_KAFKA_RESP_ERR_NO_ERROR)
		return;

	pthread_mutex_lock(&sink->lock);
	sink->errors++;
	pthread_mutex_unlock(&sink->lock);
	log_kafka_warn_throttled(sink, rd_kafka_err2str(rkmessage->err));
}

static int log_kafka_parse_query_key(const char *query, char **out_key)
{
	const char *key;
	const char *end;
	size_t len;
	char *copy;

	if (!query || !query[0] || !out_key)
		return 0;

	key = strstr(query, "key=");
	if (!key)
		return 0;

	key += 4;
	end = strchr(key, '&');
	len = end ? (size_t)(end - key) : strlen(key);
	if (!len)
		return 0;

	copy = malloc(len + 1);
	if (!copy)
		return -1;

	memcpy(copy, key, len);
	copy[len] = '\0';
	*out_key = copy;
	return 1;
}

static int log_kafka_apply_conf_kv(rd_kafka_conf_t *conf, const char *name, const char *value, char *errstr, size_t errlen)
{
	if (!conf || !name || !name[0] || !value || !value[0])
		return -1;

	if (rd_kafka_conf_set(conf, name, value, errstr, errlen) != RD_KAFKA_CONF_OK)
		return -1;
	return 1;
}

static int log_kafka_apply_query_opts(rd_kafka_conf_t *conf, const char *query, char *errstr, size_t errlen)
{
	char *copy;
	char *token;
	char *saveptr = NULL;

	if (!conf || !query || !query[0])
		return 0;

	copy = strdup(query);
	if (!copy)
		return -1;

	token = strtok_r(copy, "&", &saveptr);
	while (token)
	{
		char *eq = strchr(token, '=');
		if (eq) {
			const char *name;
			const char *value;
			*eq = '\0';
			name = token;
			value = eq + 1;
			if (strcmp(name, "key") && name[0] && value[0] &&
			    log_kafka_apply_conf_kv(conf, name, value, errstr, errlen) < 0)
			{
				free(copy);
				return -1;
			}
		}
		token = strtok_r(NULL, "&", &saveptr);
	}

	free(copy);
	return 0;
}

static int log_kafka_apply_json_opts(rd_kafka_conf_t *conf, json_t *opts, char *errstr, size_t errlen)
{
	const char *name;
	json_t *value;

	if (!conf || !opts || !json_is_object(opts))
		return 0;

	json_object_foreach(opts, name, value)
	{
		const char *val = NULL;
		char numbuf[64];
		if (!name || !name[0])
			continue;
		if (json_is_string(value))
			val = json_string_value(value);
		else if (json_is_integer(value)) {
			snprintf(numbuf, sizeof(numbuf), "%" JSON_INTEGER_FORMAT, json_integer_value(value));
			val = numbuf;
		}
		else if (json_is_true(value))
			val = "true";
		else if (json_is_false(value))
			val = "false";

		if (!val || !val[0])
			continue;
		if (log_kafka_apply_conf_kv(conf, name, val, errstr, errlen) < 0)
			return -1;
	}

	return 0;
}

static int log_kafka_parse_dest(const char *dest, char **brokers, char **topic, char **query)
{
	const char *tmp;
	const char *slash;
	const char *qmark;
	size_t blen;
	size_t tlen;
	char *bcopy;
	char *tcopy;

	if (!dest || !brokers || !topic)
		return -1;

	if (strncmp(dest, "kafka://", 8))
		return -1;

	tmp = dest + 8;
	slash = strchr(tmp, '/');
	if (!slash || slash == tmp)
		return -1;

	qmark = strchr(slash, '?');
	blen = (size_t)(slash - tmp);
	tlen = qmark ? (size_t)(qmark - slash - 1) : strlen(slash + 1);
	if (!blen || !tlen)
		return -1;

	bcopy = malloc(blen + 1);
	tcopy = malloc(tlen + 1);
	if (!bcopy || !tcopy)
	{
		free(bcopy);
		free(tcopy);
		return -1;
	}

	memcpy(bcopy, tmp, blen);
	bcopy[blen] = '\0';
	memcpy(tcopy, slash + 1, tlen);
	tcopy[tlen] = '\0';

	*brokers = bcopy;
	*topic = tcopy;
	if (query) {
		if (qmark && qmark[1])
			*query = strdup(qmark + 1);
		else
			*query = NULL;
	}

	return 0;
}

static void log_kafka_sink_destroy(log_kafka_sink *sink)
{
	if (!sink)
		return;

	if (sink->rkt)
	{
		rd_kafka_topic_destroy(sink->rkt);
		sink->rkt = NULL;
	}

	if (sink->rk)
	{
		rd_kafka_flush(sink->rk, 1000);
		rd_kafka_destroy(sink->rk);
		sink->rk = NULL;
	}

	pthread_mutex_destroy(&sink->lock);
	free(sink->brokers);
	free(sink->topic);
	free(sink->query);
	free(sink->msg_key);
	free(sink);
}

static log_kafka_sink *log_kafka_sink_create(void)
{
	log_kafka_sink *sink = calloc(1, sizeof(*sink));

	if (!sink)
		return NULL;

	pthread_mutex_init(&sink->lock, NULL);
	return sink;
}

static int log_kafka_sink_configure(log_kafka_sink *sink, const log_channel *ch)
{
	char errstr[512];
	rd_kafka_conf_t *conf;

	if (!sink || !sink->brokers || !sink->topic)
		return -1;

	if (sink->rk)
	{
		if (sink->rkt)
		{
			rd_kafka_topic_destroy(sink->rkt);
			sink->rkt = NULL;
		}
		rd_kafka_flush(sink->rk, 1000);
		rd_kafka_destroy(sink->rk);
		sink->rk = NULL;
	}

	conf = rd_kafka_conf_new();
	if (!conf)
		return -1;

	if (rd_kafka_conf_set(conf, "bootstrap.servers", sink->brokers, errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK)
		goto fail;
	if (rd_kafka_conf_set(conf, "queue.buffering.max.messages",
	    "10000", errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK)
		goto fail;
	if (rd_kafka_conf_set(conf, "queue.buffering.max.kbytes",
	    "10240", errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK)
		goto fail;
	if (rd_kafka_conf_set(conf, "message.timeout.ms", "30000", errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK)
		goto fail;
	if (rd_kafka_conf_set(conf, "linger.ms", "5", errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK)
		goto fail;
	if (rd_kafka_conf_set(conf, "acks", "1", errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK)
		goto fail;
	if (rd_kafka_conf_set(conf, "socket.timeout.ms", "60000", errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK)
		goto fail;
	if (log_kafka_apply_query_opts(conf, sink->query, errstr, sizeof(errstr)) < 0)
		goto fail;
	if (ch && log_kafka_apply_json_opts(conf, ch->kafka_options, errstr, sizeof(errstr)) < 0)
		goto fail;

	rd_kafka_conf_set_dr_msg_cb(conf, log_kafka_delivery_cb);

	sink->rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
	if (!sink->rk)
		goto fail;

	conf = NULL;
	rd_kafka_set_log_level(sink->rk, 0);

	sink->rkt = rd_kafka_topic_new(sink->rk, sink->topic, NULL);
	if (!sink->rkt)
	{
		rd_kafka_destroy(sink->rk);
		sink->rk = NULL;
		return -1;
	}

	return 0;

fail:
	if (conf)
		rd_kafka_conf_destroy(conf);
	return -1;
}

void log_kafka_sink_open(log_channel *ch, const char *dest)
{
	log_kafka_sink *sink;
	char *brokers = NULL;
	char *topic = NULL;
	char *query = NULL;
	char *key = NULL;

	if (!ch || !dest || !dest[0])
		return;

	if (log_kafka_parse_dest(dest, &brokers, &topic, &query) != 0)
	{
		free(brokers);
		free(topic);
		free(query);
		free(key);
		return;
	}
	if (ch->kafka_key && ch->kafka_key[0])
		key = strdup(ch->kafka_key);
	else if (query && log_kafka_parse_query_key(query, &key) < 0)
		key = NULL;

	sink = ch->kafka_sink;
	if (!sink)
	{
		sink = log_kafka_sink_create();
		if (!sink)
		{
			free(brokers);
			free(topic);
			free(key);
			return;
		}
		ch->kafka_sink = sink;
	}

	if (sink->brokers)
		free(sink->brokers);
	if (sink->topic)
		free(sink->topic);
	if (sink->query)
		free(sink->query);
	if (sink->msg_key)
		free(sink->msg_key);

	sink->brokers = brokers;
	sink->topic = topic;
	sink->query = query;
	sink->msg_key = key;
	sink->closing = 0;

	ch->dest_kind = LOG_DEST_KAFKA;
	ch->socket = -1;

	if (log_kafka_sink_configure(sink, ch) != 0)
		log_kafka_warn_throttled(sink, "producer init failed");
}

void log_kafka_sink_close(log_channel *ch)
{
	log_kafka_sink *sink;

	if (!ch || !ch->kafka_sink)
		return;

	sink = ch->kafka_sink;
	ch->kafka_sink = NULL;
	ch->dest_kind = LOG_DEST_FD;
	ch->socket = fileno(stdout);

	if (sink->closing)
		return;

	sink->closing = 1;
	log_kafka_sink_destroy(sink);
}

int log_kafka_sink_write(log_channel *ch, const char *data, size_t len)
{
	log_kafka_sink *sink;
	int ret;
	const char *key = NULL;
	size_t keylen = 0;

	if (!ch || !data || !len)
		return -1;

	sink = ch->kafka_sink;
	if (!sink || sink->closing || !sink->rk || !sink->rkt)
		return -1;

	if (sink->msg_key)
	{
		key = sink->msg_key;
		keylen = strlen(key);
	}

	ret = rd_kafka_produce(sink->rkt, RD_KAFKA_PARTITION_UA, RD_KAFKA_MSG_F_COPY,
	    (void *)data, len, key, (int)keylen, sink);

	if (ret == -1)
	{
		rd_kafka_resp_err_t err = rd_kafka_last_error();

		pthread_mutex_lock(&sink->lock);
		if (err == RD_KAFKA_RESP_ERR__QUEUE_FULL)
			sink->dropped++;
		else
			sink->errors++;
		pthread_mutex_unlock(&sink->lock);

		log_kafka_warn_throttled(sink, rd_kafka_err2str(err));
		return -1;
	}

	rd_kafka_poll(sink->rk, 0);
	return 0;
}

int log_channel_is_kafka(const log_channel *ch)
{
	return ch && ch->dest_kind == LOG_DEST_KAFKA;
}

uint64_t log_kafka_sink_dropped(const log_channel *ch)
{
	log_kafka_sink *sink;

	if (!ch || !ch->kafka_sink)
		return 0;

	sink = ch->kafka_sink;
	return sink->dropped;
}
