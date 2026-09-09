#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <pcre.h>
#include <librdkafka/rdkafka.h>
#include "common/logs.h"
#include "common/selector.h"
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "parsers/kafka.h"
#include "main.h"

#define KAFKA_PARAM_SIZE 512
#define KAFKA_ADDR_SIZE 288

typedef struct kafka_hw {
	char *topic;
	int32_t partition;
	int64_t high;
} kafka_hw;

static void kafka_re_free4(pcre *a, pcre *b, pcre *c, pcre *d)
{
	if (a)
		pcre_free(a);
	if (b)
		pcre_free(b);
	if (c)
		pcre_free(c);
	if (d)
		pcre_free(d);
}

static alligator_ht *kafka_clients = NULL;
static uv_timer_t kafka_timer;
static int kafka_timer_started = 0;

static inline void kafka_metric_family(context_arg *carg, const char *name, const char *help)
{
	namespace_metric_family_set(NULL, carg, name, METRIC_TYPE_GAUGE, help);
}

int kafka_query_param(const char *query, const char *name, char *out, size_t outlen)
{
	size_t nlen;
	const char *p;

	if (!query || !name || !name[0] || !out || outlen < 2)
		return 0;

	nlen = strlen(name);
	p = query;
	while (*p) {
		const char *amp;
		size_t vlen;

		if (*p == '?' || *p == '&')
			++p;
		if (!*p)
			break;
		if (!strncmp(p, name, nlen) && p[nlen] == '=') {
			p += nlen + 1;
			amp = strchr(p, '&');
			vlen = amp ? (size_t)(amp - p) : strlen(p);
			if (vlen >= outlen)
				vlen = outlen - 1;
			memcpy(out, p, vlen);
			out[vlen] = 0;
			return 1;
		}
		amp = strchr(p, '&');
		if (!amp)
			break;
		p = amp + 1;
	}
	return 0;
}

static int kafka_query_is_reserved(const char *name)
{
	return !strcmp(name, "topic_filter") ||
		!strcmp(name, "topic_exclude") ||
		!strcmp(name, "group_filter") ||
		!strcmp(name, "group_exclude") ||
		!strcmp(name, "key");
}

static pcre *kafka_re_compile(const char *pat)
{
	const char *err;
	int erroff;

	if (!pat || !pat[0])
		return NULL;
	return pcre_compile(pat, 0, &err, &erroff, NULL);
}

static int kafka_re_allowed(pcre *inc, pcre *exc, const char *name)
{
	int ovector[30];
	int nlen;

	if (!name)
		return 0;
	nlen = (int)strlen(name);
	if (inc && pcre_exec(inc, NULL, name, nlen, 0, 0, ovector, 30) < 0)
		return 0;
	if (exc && pcre_exec(exc, NULL, name, nlen, 0, 0, ovector, 30) >= 0)
		return 0;
	return 1;
}

int kafka_name_allowed(const char *name, const char *include_re, const char *exclude_re)
{
	pcre *inc = kafka_re_compile(include_re);
	pcre *exc = kafka_re_compile(exclude_re);
	int ok;

	if (include_re && include_re[0] && !inc)
		return 0;
	ok = kafka_re_allowed(inc, exc, name);
	if (inc)
		pcre_free(inc);
	if (exc)
		pcre_free(exc);
	return ok;
}

static int kafka_apply_query_opts(rd_kafka_conf_t *conf, const char *query, context_arg *carg)
{
	char *copy;
	char *token;
	char *saveptr = NULL;
	char errstr[512];

	if (!conf || !query || !query[0])
		return 0;

	copy = strdup(query);
	if (!copy)
		return -1;
	if (copy[0] == '?')
		memmove(copy, copy + 1, strlen(copy));

	token = strtok_r(copy, "&", &saveptr);
	while (token) {
		char *eq = strchr(token, '=');
		if (eq) {
			*eq = '\0';
			if (token[0] && eq[1] && !kafka_query_is_reserved(token)) {
				if (rd_kafka_conf_set(conf, token, eq + 1, errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK)
					carglog(carg, L_ERROR, "kafka: librdkafka conf '%s' rejected: %s\n", token, errstr);
			}
		}
		token = strtok_r(NULL, "&", &saveptr);
	}
	free(copy);
	return 0;
}

static int64_t kafka_hw_find(kafka_hw *arr, size_t n, const char *topic, int32_t part)
{
	size_t i;

	if (!arr || !topic)
		return RD_KAFKA_OFFSET_INVALID;
	for (i = 0; i < n; i++) {
		if (arr[i].partition == part && arr[i].topic && !strcmp(arr[i].topic, topic))
			return arr[i].high;
	}
	return RD_KAFKA_OFFSET_INVALID;
}

static void kafka_hw_free(kafka_hw *arr, size_t n)
{
	size_t i;

	if (!arr)
		return;
	for (i = 0; i < n; i++)
		free(arr[i].topic);
	free(arr);
}

static void kafka_collect_groups(context_arg *carg, rd_kafka_t *rk, kafka_hw *hws, size_t hw_n,
	pcre *group_inc, pcre *group_exc, int timeout_ms)
{
	const struct rd_kafka_group_list *grplist = NULL;
	rd_kafka_resp_err_t err;
	rd_kafka_queue_t *q;
	rd_kafka_AdminOptions_t *options;
	char errstr[512];
	int i;

	err = rd_kafka_list_groups(rk, NULL, &grplist, timeout_ms);
	if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
		carglog(carg, L_ERROR, "kafka: list_groups failed: %s\n", rd_kafka_err2str(err));
		return;
	}

	kafka_metric_family(carg, "kafka_consumergroup_members",
		"Amount of members in a consumer group.");
	kafka_metric_family(carg, "kafka_consumergroup_current_offset",
		"Current offset of a consumer group at topic/partition.");
	kafka_metric_family(carg, "kafka_consumergroup_current_offset_sum",
		"Current offset of a consumer group at topic for all partitions.");
	kafka_metric_family(carg, "kafka_consumergroup_lag",
		"Current approximate lag of a consumer group at topic/partition.");
	kafka_metric_family(carg, "kafka_consumergroup_lag_sum",
		"Current approximate lag of a consumer group at topic for all partitions.");

	q = rd_kafka_queue_new(rk);
	options = rd_kafka_AdminOptions_new(rk, RD_KAFKA_ADMIN_OP_LISTCONSUMERGROUPOFFSETS);
	rd_kafka_AdminOptions_set_request_timeout(options, timeout_ms, errstr, sizeof(errstr));

	for (i = 0; i < grplist->group_cnt; i++) {
		const struct rd_kafka_group_info *gi = &grplist->groups[i];
		rd_kafka_ListConsumerGroupOffsets_t *req;
		rd_kafka_event_t *ev;
		const rd_kafka_ListConsumerGroupOffsets_result_t *res;
		const rd_kafka_group_result_t **gres;
		size_t gcnt = 0;
		size_t gi_n;
		int64_t members;
		const rd_kafka_topic_partition_list_t *parts;

		if (!gi->group || !kafka_re_allowed(group_inc, group_exc, gi->group))
			continue;

		members = gi->member_cnt;
		metric_add_labels("kafka_consumergroup_members", &members, DATATYPE_INT, carg,
			"consumergroup", gi->group);

		req = rd_kafka_ListConsumerGroupOffsets_new(gi->group, NULL);
		if (!req)
			continue;
		rd_kafka_ListConsumerGroupOffsets(rk, &req, 1, options, q);
		rd_kafka_ListConsumerGroupOffsets_destroy(req);

		ev = rd_kafka_queue_poll(q, timeout_ms);
		if (!ev) {
			carglog(carg, L_ERROR, "kafka: ListConsumerGroupOffsets timeout for group %s\n", gi->group);
			continue;
		}
		if (rd_kafka_event_error(ev)) {
			carglog(carg, L_ERROR, "kafka: ListConsumerGroupOffsets %s: %s\n",
				gi->group, rd_kafka_event_error_string(ev));
			rd_kafka_event_destroy(ev);
			continue;
		}

		res = rd_kafka_event_ListConsumerGroupOffsets_result(ev);
		if (!res) {
			rd_kafka_event_destroy(ev);
			continue;
		}
		gres = rd_kafka_ListConsumerGroupOffsets_result_groups(res, &gcnt);
		for (gi_n = 0; gi_n < gcnt; gi_n++) {
			int j;
			char last_topic[256];
			int64_t offset_sum = 0;
			int64_t lag_sum = 0;
			int have_topic = 0;

			parts = rd_kafka_group_result_partitions(gres[gi_n]);
			if (!parts)
				continue;
			last_topic[0] = 0;
			for (j = 0; j < parts->cnt; j++) {
				const rd_kafka_topic_partition_t *tp = &parts->elems[j];
				char partstr[16];
				int64_t high;
				int64_t off;
				int64_t lag;

				if (!tp->topic)
					continue;
				if (tp->offset == RD_KAFKA_OFFSET_INVALID)
					continue;

				if (have_topic && strcmp(last_topic, tp->topic)) {
					metric_add_labels2("kafka_consumergroup_current_offset_sum", &offset_sum,
						DATATYPE_INT, carg, "consumergroup", gi->group, "topic", last_topic);
					metric_add_labels2("kafka_consumergroup_lag_sum", &lag_sum,
						DATATYPE_INT, carg, "consumergroup", gi->group, "topic", last_topic);
					offset_sum = 0;
					lag_sum = 0;
				}
				strlcpy(last_topic, tp->topic, sizeof(last_topic));
				have_topic = 1;

				snprintf(partstr, sizeof(partstr), "%" PRId32, tp->partition);
				off = tp->offset;
				metric_add_labels3("kafka_consumergroup_current_offset", &off, DATATYPE_INT, carg,
					"consumergroup", gi->group, "topic", tp->topic, "partition", partstr);
				offset_sum += off;

				high = kafka_hw_find(hws, hw_n, tp->topic, tp->partition);
				if (high == RD_KAFKA_OFFSET_INVALID)
					lag = -1;
				else {
					lag = high - off;
					lag_sum += lag;
				}
				metric_add_labels3("kafka_consumergroup_lag", &lag, DATATYPE_INT, carg,
					"consumergroup", gi->group, "topic", tp->topic, "partition", partstr);
			}
			if (have_topic) {
				metric_add_labels2("kafka_consumergroup_current_offset_sum", &offset_sum,
					DATATYPE_INT, carg, "consumergroup", gi->group, "topic", last_topic);
				metric_add_labels2("kafka_consumergroup_lag_sum", &lag_sum,
					DATATYPE_INT, carg, "consumergroup", gi->group, "topic", last_topic);
			}
		}
		rd_kafka_event_destroy(ev);
	}

	rd_kafka_AdminOptions_destroy(options);
	rd_kafka_queue_destroy(q);
	rd_kafka_group_list_destroy(grplist);
}

static void kafka_collect(context_arg *carg)
{
	rd_kafka_conf_t *conf;
	rd_kafka_t *rk;
	char errstr[512];
	char brokers[KAFKA_ADDR_SIZE];
	char topic_filter[KAFKA_PARAM_SIZE];
	char topic_exclude[KAFKA_PARAM_SIZE];
	char group_filter[KAFKA_PARAM_SIZE];
	char group_exclude[KAFKA_PARAM_SIZE];
	const char *port;
	const struct rd_kafka_metadata *md = NULL;
	rd_kafka_resp_err_t err;
	kafka_hw *hws = NULL;
	size_t hw_n = 0;
	size_t hw_cap = 0;
	pcre *topic_inc;
	pcre *topic_exc;
	pcre *group_inc;
	pcre *group_exc;
	int timeout_ms;
	int i;

	if (!carg)
		return;
	if (carg->data_lock)
		return;
	carg->data_lock = 1;

	timeout_ms = carg->timeout > 0 ? (int)carg->timeout : 5000;
	port = carg->port[0] ? carg->port : "9092";
	snprintf(brokers, sizeof(brokers), "%s:%s", carg->host, port);

	topic_filter[0] = 0;
	topic_exclude[0] = 0;
	group_filter[0] = 0;
	group_exclude[0] = 0;
	kafka_query_param(carg->query_url, "topic_filter", topic_filter, sizeof(topic_filter));
	kafka_query_param(carg->query_url, "topic_exclude", topic_exclude, sizeof(topic_exclude));
	kafka_query_param(carg->query_url, "group_filter", group_filter, sizeof(group_filter));
	kafka_query_param(carg->query_url, "group_exclude", group_exclude, sizeof(group_exclude));
	if (!topic_filter[0])
		strlcpy(topic_filter, ".*", sizeof(topic_filter));
	if (!topic_exclude[0])
		strlcpy(topic_exclude, "^$", sizeof(topic_exclude));
	if (!group_filter[0])
		strlcpy(group_filter, ".*", sizeof(group_filter));
	if (!group_exclude[0])
		strlcpy(group_exclude, "^$", sizeof(group_exclude));

	topic_inc = kafka_re_compile(topic_filter);
	topic_exc = kafka_re_compile(topic_exclude);
	group_inc = kafka_re_compile(group_filter);
	group_exc = kafka_re_compile(group_exclude);
	if ((topic_filter[0] && !topic_inc) || (group_filter[0] && !group_inc)) {
		carglog(carg, L_ERROR, "kafka: invalid topic_filter or group_filter PCRE\n");
		kafka_re_free4(topic_inc, topic_exc, group_inc, group_exc);
		carg->data_lock = 0;
		return;
	}

	conf = rd_kafka_conf_new();
	if (rd_kafka_conf_set(conf, "bootstrap.servers", brokers, errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
		carglog(carg, L_ERROR, "kafka: bootstrap.servers=%s: %s\n", brokers, errstr);
		rd_kafka_conf_destroy(conf);
		kafka_re_free4(topic_inc, topic_exc, group_inc, group_exc);
		carg->data_lock = 0;
		return;
	}
	rd_kafka_conf_set(conf, "client.id", "alligator-kafka", errstr, sizeof(errstr));
	rd_kafka_conf_set(conf, "group.id", "alligator-kafka-metrics", errstr, sizeof(errstr));
	rd_kafka_conf_set(conf, "enable.auto.commit", "false", errstr, sizeof(errstr));
	rd_kafka_conf_set(conf, "allow.auto.create.topics", "false", errstr, sizeof(errstr));
	rd_kafka_conf_set(conf, "log.connection.close", "false", errstr, sizeof(errstr));

	if (carg->user[0]) {
		rd_kafka_conf_set(conf, "security.protocol", "SASL_PLAINTEXT", errstr, sizeof(errstr));
		rd_kafka_conf_set(conf, "sasl.mechanism", "PLAIN", errstr, sizeof(errstr));
		rd_kafka_conf_set(conf, "sasl.username", carg->user, errstr, sizeof(errstr));
		if (carg->password[0])
			rd_kafka_conf_set(conf, "sasl.password", carg->password, errstr, sizeof(errstr));
	}

	kafka_apply_query_opts(conf, carg->query_url, carg);

	rk = rd_kafka_new(RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));
	if (!rk) {
		carglog(carg, L_ERROR, "kafka: rd_kafka_new failed: %s\n", errstr);
		kafka_re_free4(topic_inc, topic_exc, group_inc, group_exc);
		carg->data_lock = 0;
		return;
	}

	err = rd_kafka_metadata(rk, 1, NULL, &md, timeout_ms);
	if (err != RD_KAFKA_RESP_ERR_NO_ERROR || !md) {
		carglog(carg, L_ERROR, "kafka: metadata from %s failed: %s\n",
			brokers, rd_kafka_err2str(err));
		kafka_re_free4(topic_inc, topic_exc, group_inc, group_exc);
		rd_kafka_destroy(rk);
		carg->data_lock = 0;
		return;
	}

	kafka_metric_family(carg, "kafka_brokers", "Number of brokers in the Kafka cluster.");
	kafka_metric_family(carg, "kafka_broker_info", "Information about the Kafka broker.");
	kafka_metric_family(carg, "kafka_topic_partitions", "Number of partitions for this topic.");
	kafka_metric_family(carg, "kafka_topic_partition_current_offset",
		"Current offset of a broker at topic/partition.");
	kafka_metric_family(carg, "kafka_topic_partition_oldest_offset",
		"Oldest offset of a broker at topic/partition.");
	kafka_metric_family(carg, "kafka_topic_partition_leader",
		"Leader broker ID of this topic/partition.");
	kafka_metric_family(carg, "kafka_topic_partition_replicas",
		"Number of replicas for this topic/partition.");
	kafka_metric_family(carg, "kafka_topic_partition_in_sync_replica",
		"Number of in-sync replicas for this topic/partition.");
	kafka_metric_family(carg, "kafka_topic_partition_leader_is_preferred",
		"1 if topic/partition is using the preferred broker.");
	kafka_metric_family(carg, "kafka_topic_partition_under_replicated_partition",
		"1 if topic/partition is under-replicated.");

	{
		int64_t nbrokers = md->broker_cnt;
		metric_add_auto("kafka_brokers", &nbrokers, DATATYPE_INT, carg);
	}

	for (i = 0; i < md->broker_cnt; i++) {
		const struct rd_kafka_metadata_broker *b = &md->brokers[i];
		char addr[KAFKA_ADDR_SIZE];
		char idstr[16];
		int64_t one = 1;

		snprintf(addr, sizeof(addr), "%s:%d", b->host ? b->host : "", b->port);
		snprintf(idstr, sizeof(idstr), "%" PRId32, b->id);
		metric_add_labels2("kafka_broker_info", &one, DATATYPE_INT, carg, "id", idstr, "address", addr);
	}

	for (i = 0; i < md->topic_cnt; i++) {
		const struct rd_kafka_metadata_topic *t = &md->topics[i];
		int64_t np;
		int p;

		if (!t->topic || t->err)
			continue;
		if (!kafka_re_allowed(topic_inc, topic_exc, t->topic))
			continue;

		np = t->partition_cnt;
		metric_add_labels("kafka_topic_partitions", &np, DATATYPE_INT, carg, "topic", t->topic);

		for (p = 0; p < t->partition_cnt; p++) {
			const struct rd_kafka_metadata_partition *part = &t->partitions[p];
			char partstr[16];
			int64_t low = 0;
			int64_t high = 0;
			int64_t leader;
			int64_t nreplicas;
			int64_t nisr;
			int64_t preferred;
			int64_t under;
			rd_kafka_resp_err_t werr;

			snprintf(partstr, sizeof(partstr), "%" PRId32, part->id);
			leader = part->leader;
			nreplicas = part->replica_cnt;
			nisr = part->isr_cnt;
			preferred = (part->replica_cnt > 0 && part->replicas && part->leader == part->replicas[0]) ? 1 : 0;
			under = (part->isr_cnt < part->replica_cnt) ? 1 : 0;

			metric_add_labels2("kafka_topic_partition_leader", &leader, DATATYPE_INT, carg,
				"topic", t->topic, "partition", partstr);
			metric_add_labels2("kafka_topic_partition_replicas", &nreplicas, DATATYPE_INT, carg,
				"topic", t->topic, "partition", partstr);
			metric_add_labels2("kafka_topic_partition_in_sync_replica", &nisr, DATATYPE_INT, carg,
				"topic", t->topic, "partition", partstr);
			metric_add_labels2("kafka_topic_partition_leader_is_preferred", &preferred, DATATYPE_INT, carg,
				"topic", t->topic, "partition", partstr);
			metric_add_labels2("kafka_topic_partition_under_replicated_partition", &under, DATATYPE_INT, carg,
				"topic", t->topic, "partition", partstr);

			werr = rd_kafka_query_watermark_offsets(rk, t->topic, part->id, &low, &high, timeout_ms);
			if (werr != RD_KAFKA_RESP_ERR_NO_ERROR) {
				carglog(carg, L_ERROR, "kafka: watermarks %s/%" PRId32 ": %s\n",
					t->topic, part->id, rd_kafka_err2str(werr));
				continue;
			}
			metric_add_labels2("kafka_topic_partition_oldest_offset", &low, DATATYPE_INT, carg,
				"topic", t->topic, "partition", partstr);
			metric_add_labels2("kafka_topic_partition_current_offset", &high, DATATYPE_INT, carg,
				"topic", t->topic, "partition", partstr);

			if (hw_n == hw_cap) {
				size_t ncap = hw_cap ? hw_cap * 2 : 32;
				kafka_hw *nh = realloc(hws, ncap * sizeof(*nh));
				if (!nh)
					continue;
				hws = nh;
				hw_cap = ncap;
			}
			hws[hw_n].topic = strdup(t->topic);
			hws[hw_n].partition = part->id;
			hws[hw_n].high = high;
			hw_n++;
		}
	}

	kafka_collect_groups(carg, rk, hws, hw_n, group_inc, group_exc, timeout_ms);
	kafka_hw_free(hws, hw_n);
	kafka_re_free4(topic_inc, topic_exc, group_inc, group_exc);
	rd_kafka_metadata_destroy(md);
	rd_kafka_destroy(rk);
	carg->parser_status = 1;
	carg->data_lock = 0;
	carglog(carg, L_DEBUG, "kafka cycle done: brokers=%s\n", brokers);
}

static void kafka_run_single_foreach(void *arg)
{
	kafka_collect(arg);
}

static void kafka_timer_cb(uv_timer_t *handle)
{
	(void)handle;
	if (kafka_clients)
		alligator_ht_foreach(kafka_clients, kafka_run_single_foreach);
}

int kafka_aggregator(context_arg *carg)
{
	if (!kafka_clients)
		kafka_clients = alligator_ht_init(NULL);

	if (!carg->key) {
		carg->key = malloc(255);
		snprintf(carg->key, 255, "%s:%s", carg->host, carg->port[0] ? carg->port : "9092");
	}
	alligator_ht_insert(kafka_clients, &(carg->node), carg, tommy_strhash_u32(0, carg->key));

	if (!kafka_timer_started) {
		uv_timer_init(ac->loop, &kafka_timer);
		uv_timer_start(&kafka_timer, kafka_timer_cb, ac->aggregator_startup, ac->aggregator_repeat);
		kafka_timer_started = 1;
	}

	return 1;
}

void kafka_parser_push(void)
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("kafka");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler) * actx->handlers);

	actx->handler[0].name = NULL;
	actx->handler[0].validator = NULL;
	actx->handler[0].smart_aggregator_replace = kafka_aggregator;
	strlcpy(actx->handler[0].key, "kafka", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
