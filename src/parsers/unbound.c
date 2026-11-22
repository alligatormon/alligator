#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/validator.h"
#include "metric/metric_types.h"
#include "main.h"

#define UNBOUND_NAME_SIZE 1024
#define UNBOUND_HIST_MAX 128

typedef struct unbound_hist_bucket {
	double le;
	uint64_t count;
} unbound_hist_bucket;

static int unbound_hist_cmp(const void *a, const void *b)
{
	const unbound_hist_bucket *x = a;
	const unbound_hist_bucket *y = b;
	if (x->le < y->le)
		return -1;
	if (x->le > y->le)
		return 1;
	return 0;
}

static void unbound_emit_u64(context_arg *carg, const char *name, uint8_t type, const char *help, uint64_t val)
{
	namespace_metric_family_set(NULL, carg, name, type, help);
	metric_add_auto((char *)name, &val, DATATYPE_UINT, carg);
}

static void unbound_emit_f64(context_arg *carg, const char *name, uint8_t type, const char *help, double val)
{
	namespace_metric_family_set(NULL, carg, name, type, help);
	metric_add_auto((char *)name, &val, DATATYPE_DOUBLE, carg);
}

static void unbound_emit_u64_l1(context_arg *carg, const char *name, uint8_t type, const char *help,
	const char *lname, char *lval, uint64_t val)
{
	namespace_metric_family_set(NULL, carg, name, type, help);
	metric_add_labels((char *)name, &val, DATATYPE_UINT, carg, (char *)lname, lval);
}

static void unbound_emit_f64_l1(context_arg *carg, const char *name, uint8_t type, const char *help,
	const char *lname, char *lval, double val)
{
	namespace_metric_family_set(NULL, carg, name, type, help);
	metric_add_labels((char *)name, &val, DATATYPE_DOUBLE, carg, (char *)lname, lval);
}

/* thread0 → "0" */
static void unbound_thread_id(const char *thread_key, char *out, size_t outsz)
{
	const char *p = thread_key;
	if (!strncmp(p, "thread", 6))
		p += 6;
	strlcpy(out, p, outsz);
}

static void unbound_emit_histogram(context_arg *carg, unbound_hist_bucket *buckets, size_t n,
	uint64_t count, double avg)
{
	namespace_metric_family_set(NULL, carg, "unbound_response_time_seconds", METRIC_TYPE_HISTOGRAM,
		"Query response time in seconds.");

	if (!n)
		return;

	qsort(buckets, n, sizeof(*buckets), unbound_hist_cmp);

	uint64_t cum = 0;
	char lebuf[64];
	for (size_t i = 0; i < n; i++)
	{
		cum += buckets[i].count;
		buckets[i].count = cum;
		/* Keep enough precision for sub-ms Unbound bucket edges. */
		snprintf(lebuf, sizeof(lebuf), "%.6g", buckets[i].le);
		metric_add_labels("unbound_response_time_seconds_bucket", &buckets[i].count,
			DATATYPE_UINT, carg, "le", lebuf);
	}

	snprintf(lebuf, sizeof(lebuf), "+Inf");
	metric_add_labels("unbound_response_time_seconds_bucket", &count,
		DATATYPE_UINT, carg, "le", lebuf);

	metric_add_auto("unbound_response_time_seconds_count", &count, DATATYPE_UINT, carg);
	double sum = avg * (double)count;
	metric_add_auto("unbound_response_time_seconds_sum", &sum, DATATYPE_DOUBLE, carg);
}

void unbound_handler(char *metrics, size_t size, context_arg *carg)
{
	uint64_t i = 0;
	char key[UNBOUND_NAME_SIZE];
	char tmp[UNBOUND_NAME_SIZE];
	char thread_id[32];
	uint64_t copysize;

	unbound_hist_bucket hist[UNBOUND_HIST_MAX];
	size_t hist_n = 0;
	uint64_t hist_count = 0;
	double hist_avg = 0;
	int have_avg = 0;

	for (; i < size; i++)
	{
		copysize = strcspn(metrics + i, " =");
		size_t key_copy = copysize < (sizeof(key) - 1) ? copysize : (sizeof(key) - 1);
		strlcpy(key, metrics + i, key_copy + 1);
		i += copysize;
		i += strspn(metrics + i, "= ");

		if (!strncmp(key, "histogram.", 10))
		{
			char *to = strstr(key + 10, ".to.");
			if (!to || hist_n >= UNBOUND_HIST_MAX)
			{
				i += strcspn(metrics + i, "\n");
				continue;
			}
			double le = strtod(to + 4, NULL);
			uint64_t val = strtoull(metrics + i, NULL, 10);
			hist[hist_n].le = le;
			hist[hist_n].count = val;
			hist_n++;
			hist_count += val;
		}
		else if (!strcmp(key, "total.recursion.time.avg"))
		{
			hist_avg = strtod(metrics + i, NULL);
			have_avg = 1;
			unbound_emit_f64(carg, "unbound_recursion_time_seconds_avg", METRIC_TYPE_GAUGE,
				"Average recursive query processing time in seconds.", hist_avg);
		}
		else if (!strcmp(key, "total.recursion.time.median"))
		{
			double dval = strtod(metrics + i, NULL);
			unbound_emit_f64(carg, "unbound_recursion_time_seconds_median", METRIC_TYPE_GAUGE,
				"Median recursive query processing time in seconds.", dval);
		}
		else if (!strncmp(key, "thread", 6) && strchr(key, '.'))
		{
			char *dot = strchr(key, '.');
			size_t tlen = (size_t)(dot - key);
			size_t tcopy = tlen < sizeof(tmp) - 1 ? tlen : sizeof(tmp) - 1;
			strlcpy(tmp, key, tcopy + 1);
			unbound_thread_id(tmp, thread_id, sizeof(thread_id));
			const char *rest = dot + 1;

			if (!strcmp(rest, "recursion.time.avg"))
			{
				double dval = strtod(metrics + i, NULL);
				unbound_emit_f64_l1(carg, "unbound_thread_recursion_time_seconds_avg", METRIC_TYPE_GAUGE,
					"Per-thread average recursive processing time in seconds.", "thread", thread_id, dval);
			}
			else if (!strcmp(rest, "recursion.time.median"))
			{
				double dval = strtod(metrics + i, NULL);
				unbound_emit_f64_l1(carg, "unbound_thread_recursion_time_seconds_median", METRIC_TYPE_GAUGE,
					"Per-thread median recursive processing time in seconds.", "thread", thread_id, dval);
			}
			else if (!strcmp(rest, "num.queries"))
			{
				uint64_t val = strtoull(metrics + i, NULL, 10);
				unbound_emit_u64_l1(carg, "unbound_queries_total", METRIC_TYPE_COUNTER,
					"Total number of queries received.", "thread", thread_id, val);
			}
			else if (!strcmp(rest, "num.cachehits"))
			{
				uint64_t val = strtoull(metrics + i, NULL, 10);
				unbound_emit_u64_l1(carg, "unbound_cache_hits_total", METRIC_TYPE_COUNTER,
					"Queries answered from cache.", "thread", thread_id, val);
			}
			else if (!strcmp(rest, "num.cachemiss"))
			{
				uint64_t val = strtoull(metrics + i, NULL, 10);
				unbound_emit_u64_l1(carg, "unbound_cache_misses_total", METRIC_TYPE_COUNTER,
					"Cache queries that needed recursion.", "thread", thread_id, val);
			}
			else if (!strcmp(rest, "num.prefetch"))
			{
				uint64_t val = strtoull(metrics + i, NULL, 10);
				unbound_emit_u64_l1(carg, "unbound_prefetches_total", METRIC_TYPE_COUNTER,
					"Cache prefetches performed.", "thread", thread_id, val);
			}
			else if (!strcmp(rest, "num.expired"))
			{
				uint64_t val = strtoull(metrics + i, NULL, 10);
				unbound_emit_u64_l1(carg, "unbound_expired_total", METRIC_TYPE_COUNTER,
					"Expired cache entries served.", "thread", thread_id, val);
			}
			else if (!strcmp(rest, "num.recursivereplies"))
			{
				uint64_t val = strtoull(metrics + i, NULL, 10);
				unbound_emit_u64_l1(carg, "unbound_recursive_replies_total", METRIC_TYPE_COUNTER,
					"Replies that needed recursive processing.", "thread", thread_id, val);
			}
			else if (!strcmp(rest, "requestlist.current.all"))
			{
				uint64_t val = strtoull(metrics + i, NULL, 10);
				unbound_emit_u64_l1(carg, "unbound_request_list_current_all", METRIC_TYPE_GAUGE,
					"Current request list size including internal queries.", "thread", thread_id, val);
			}
			else if (!strcmp(rest, "requestlist.current.user"))
			{
				uint64_t val = strtoull(metrics + i, NULL, 10);
				unbound_emit_u64_l1(carg, "unbound_request_list_current_user", METRIC_TYPE_GAUGE,
					"Current request list size for client queries.", "thread", thread_id, val);
			}
			else if (!strcmp(rest, "requestlist.exceeded"))
			{
				uint64_t val = strtoull(metrics + i, NULL, 10);
				unbound_emit_u64_l1(carg, "unbound_request_list_exceeded_total", METRIC_TYPE_COUNTER,
					"Queries dropped because the request list was full.", "thread", thread_id, val);
			}
			else if (!strcmp(rest, "requestlist.overwritten"))
			{
				uint64_t val = strtoull(metrics + i, NULL, 10);
				unbound_emit_u64_l1(carg, "unbound_request_list_overwritten_total", METRIC_TYPE_COUNTER,
					"Request list entries overwritten by newer ones.", "thread", thread_id, val);
			}
			else if (!strcmp(rest, "tcpusage"))
			{
				uint64_t val = strtoull(metrics + i, NULL, 10);
				unbound_emit_u64_l1(carg, "unbound_tcpusage", METRIC_TYPE_GAUGE,
					"Per-thread TCP buffers in use.", "thread", thread_id, val);
			}
			else
			{
				/* Fallback: unbound_thread_<rest_normalized>{thread} */
				uint64_t val = strtoull(metrics + i, NULL, 10);
				char mname[UNBOUND_NAME_SIZE];
				snprintf(mname, sizeof(mname), "unbound_thread_%s", rest);
				metric_name_normalizer(mname, strlen(mname));
				namespace_metric_family_set(NULL, carg, mname, METRIC_TYPE_GAUGE,
					"Unbound per-thread exported metric value.");
				metric_add_labels(mname, &val, DATATYPE_UINT, carg, "thread", thread_id);
			}
		}
		else if (!strcmp(key, "time.up"))
		{
			uint64_t val = strtoull(metrics + i, NULL, 10);
			unbound_emit_u64(carg, "unbound_time_up_seconds_total", METRIC_TYPE_COUNTER,
				"Uptime since Unbound server boot in seconds.", val);
		}
		else if (!strcmp(key, "time.now"))
		{
			double dval = strtod(metrics + i, NULL);
			unbound_emit_f64(carg, "unbound_time_now_seconds", METRIC_TYPE_GAUGE,
				"Current Unbound time in seconds since epoch.", dval);
		}
		else if (!strcmp(key, "time.elapsed"))
		{
			double dval = strtod(metrics + i, NULL);
			unbound_emit_f64(carg, "unbound_time_elapsed_seconds", METRIC_TYPE_COUNTER,
				"Seconds since last statistics printout.", dval);
		}
		else if (!strcmp(key, "total.tcpusage"))
		{
			uint64_t val = strtoull(metrics + i, NULL, 10);
			unbound_emit_u64(carg, "unbound_tcpusage", METRIC_TYPE_GAUGE,
				"Total TCP buffers in use.", val);
		}
		else if (!strcmp(key, "num.rrset.bogus"))
		{
			uint64_t val = strtoull(metrics + i, NULL, 10);
			unbound_emit_u64(carg, "unbound_rrset_bogus_total", METRIC_TYPE_COUNTER,
				"RRsets marked bogus by the validator.", val);
		}
		else if (!strcmp(key, "unwanted.queries"))
		{
			uint64_t val = strtoull(metrics + i, NULL, 10);
			unbound_emit_u64(carg, "unbound_unwanted_queries_total", METRIC_TYPE_COUNTER,
				"Queries refused or dropped by access control.", val);
		}
		else if (!strcmp(key, "unwanted.replies"))
		{
			uint64_t val = strtoull(metrics + i, NULL, 10);
			unbound_emit_u64(carg, "unbound_unwanted_replies_total", METRIC_TYPE_COUNTER,
				"Unwanted or unsolicited replies.", val);
		}
		else if (!strcmp(key, "msg.cache.count"))
		{
			uint64_t val = strtoull(metrics + i, NULL, 10);
			unbound_emit_u64(carg, "unbound_msg_cache_count", METRIC_TYPE_GAUGE,
				"Number of messages in cache.", val);
			unbound_emit_u64_l1(carg, "unbound_cache_count", METRIC_TYPE_GAUGE,
				"Unbound cache object counts.", "cache", "msg", val);
		}
		else if (!strcmp(key, "rrset.cache.count"))
		{
			uint64_t val = strtoull(metrics + i, NULL, 10);
			unbound_emit_u64(carg, "unbound_rrset_cache_count", METRIC_TYPE_GAUGE,
				"Number of RRsets in cache.", val);
			unbound_emit_u64_l1(carg, "unbound_cache_count", METRIC_TYPE_GAUGE,
				"Unbound cache object counts.", "cache", "rrset", val);
		}
		else if (!strcmp(key, "infra.cache.count"))
		{
			uint64_t val = strtoull(metrics + i, NULL, 10);
			unbound_emit_u64(carg, "unbound_infra_cache_count", METRIC_TYPE_GAUGE,
				"Number of infra cache entries.", val);
			unbound_emit_u64_l1(carg, "unbound_cache_count", METRIC_TYPE_GAUGE,
				"Unbound cache object counts.", "cache", "infra", val);
		}
		else if (!strcmp(key, "key.cache.count"))
		{
			uint64_t val = strtoull(metrics + i, NULL, 10);
			unbound_emit_u64_l1(carg, "unbound_cache_count", METRIC_TYPE_GAUGE,
				"Unbound cache object counts.", "cache", "key", val);
		}
		else if (!strncmp(key, "mem.cache.", 10))
		{
			uint64_t val = strtoull(metrics + i, NULL, 10);
			strlcpy(tmp, key + 10, sizeof(tmp));
			unbound_emit_u64_l1(carg, "unbound_memory_caches_bytes", METRIC_TYPE_GAUGE,
				"Memory in bytes used by caches.", "cache", tmp, val);
		}
		else if (!strncmp(key, "mem.mod.", 8))
		{
			uint64_t val = strtoull(metrics + i, NULL, 10);
			strlcpy(tmp, key + 8, sizeof(tmp));
			unbound_emit_u64_l1(carg, "unbound_memory_modules_bytes", METRIC_TYPE_GAUGE,
				"Memory in bytes used by modules.", "module", tmp, val);
		}
		else if (!strncmp(key, "num.answer.rcode.", 17))
		{
			uint64_t val = strtoull(metrics + i, NULL, 10);
			strlcpy(tmp, key + 17, sizeof(tmp));
			unbound_emit_u64_l1(carg, "unbound_answer_rcodes_total", METRIC_TYPE_COUNTER,
				"Answers by response code.", "rcode", tmp, val);
		}
		else if (!strncmp(key, "num.query.type.", 15))
		{
			uint64_t val = strtoull(metrics + i, NULL, 10);
			strlcpy(tmp, key + 15, sizeof(tmp));
			unbound_emit_u64_l1(carg, "unbound_query_types_total", METRIC_TYPE_COUNTER,
				"Queries by DNS type.", "type", tmp, val);
		}
		else if (!strncmp(key, "num.query.flags.", 16))
		{
			uint64_t val = strtoull(metrics + i, NULL, 10);
			strlcpy(tmp, key + 16, sizeof(tmp));
			unbound_emit_u64_l1(carg, "unbound_query_flags_total", METRIC_TYPE_COUNTER,
				"Queries with a given header flag.", "flag", tmp, val);
		}
		else if (!strcmp(key, "num.query.edns.present"))
		{
			uint64_t val = strtoull(metrics + i, NULL, 10);
			unbound_emit_u64(carg, "unbound_query_edns_present_total", METRIC_TYPE_COUNTER,
				"Queries with an EDNS OPT record present.", val);
		}
		else if (!strcmp(key, "num.query.edns.DO"))
		{
			uint64_t val = strtoull(metrics + i, NULL, 10);
			unbound_emit_u64(carg, "unbound_query_edns_DO_total", METRIC_TYPE_COUNTER,
				"Queries with EDNS DO bit set.", val);
		}
		else if (!strcmp(key, "num.query.tcp"))
		{
			uint64_t val = strtoull(metrics + i, NULL, 10);
			unbound_emit_u64(carg, "unbound_query_tcp_total", METRIC_TYPE_COUNTER,
				"Queries received over TCP.", val);
		}
		else if (!strcmp(key, "num.query.tcpout"))
		{
			uint64_t val = strtoull(metrics + i, NULL, 10);
			unbound_emit_u64(carg, "unbound_query_tcpout_total", METRIC_TYPE_COUNTER,
				"Outgoing TCP queries from Unbound.", val);
		}
		else if (!strcmp(key, "num.query.udpout"))
		{
			uint64_t val = strtoull(metrics + i, NULL, 10);
			unbound_emit_u64(carg, "unbound_query_udpout_total", METRIC_TYPE_COUNTER,
				"Outgoing UDP queries from Unbound.", val);
		}
		else if (!strcmp(key, "num.query.tls"))
		{
			uint64_t val = strtoull(metrics + i, NULL, 10);
			unbound_emit_u64(carg, "unbound_query_tls_total", METRIC_TYPE_COUNTER,
				"Queries received over TLS.", val);
		}
		else if (!strcmp(key, "num.query.https"))
		{
			uint64_t val = strtoull(metrics + i, NULL, 10);
			unbound_emit_u64(carg, "unbound_query_https_total", METRIC_TYPE_COUNTER,
				"DoH queries received.", val);
		}
		else if (!strncmp(key, "total.num.", 10) || !strncmp(key, "total.requestlist.", 18))
		{
			/* Aggregate totals → gauges/counters with last component as label value. */
			uint64_t val = strtoull(metrics + i, NULL, 10);
			char *lastdot = key;
			char *p = key;
			while (*p)
			{
				if (*p == '.')
					lastdot = p + 1;
				++p;
			}
			if (!strncmp(key, "total.num.", 10))
			{
				uint8_t type = METRIC_TYPE_COUNTER;
				unbound_emit_u64_l1(carg, "unbound_total_num", type,
					"Unbound total.num.* counters.", "num", lastdot, val);
			}
			else
			{
				char mname[UNBOUND_NAME_SIZE];
				snprintf(mname, sizeof(mname), "unbound_total_requestlist_%s", lastdot);
				metric_name_normalizer(mname, strlen(mname));
				namespace_metric_family_set(NULL, carg, mname, METRIC_TYPE_GAUGE,
					"Unbound total requestlist metric.");
				metric_add_auto(mname, &val, DATATYPE_UINT, carg);
			}
		}
		else
		{
			/* Generic fallback: unbound_<key_normalized> as gauge. */
			uint64_t val = strtoull(metrics + i, NULL, 10);
			char mname[UNBOUND_NAME_SIZE];
			snprintf(mname, sizeof(mname), "unbound_%s", key);
			metric_name_normalizer(mname, strlen(mname));
			namespace_metric_family_set(NULL, carg, mname, METRIC_TYPE_GAUGE,
				"Unbound exported metric value.");
			metric_add_auto(mname, &val, DATATYPE_UINT, carg);
		}

		i += strcspn(metrics + i, "\n");
	}

	unbound_emit_histogram(carg, hist, hist_n, hist_count, have_avg ? hist_avg : 0);

	uint64_t up = 1;
	namespace_metric_family_set(NULL, carg, "unbound_up", METRIC_TYPE_GAUGE,
		"1 if the last Unbound stats scrape succeeded.");
	metric_add_auto("unbound_up", &up, DATATYPE_UINT, carg);

	carg->parser_status = 1;
}

int8_t unbound_validator(context_arg *carg, char *data, size_t size)
{
	if (size < 4500)
		return 0;
	char *ret = strstr(data + 4500, "key.cache.count");
	if (ret)
		return 1;
	else
		return 0;
}

string* unbound_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_alloc("UBCT1 stats_noreset\n", 0);
}

void unbound_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("unbound");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = unbound_handler;
	actx->handler[0].validator = unbound_validator;
	actx->handler[0].mesg_func = unbound_mesg;
	strlcpy(actx->handler[0].key,"unbound", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
