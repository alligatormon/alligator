#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <math.h>
#include <jansson.h>
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/logs.h"
#include "main.h"

#define OPENCLAW_PATH_MAX 4096
#define OPENCLAW_TAIL_STATE 8192
#define OPENCLAW_TAIL_USAGE 65536
#define OPENCLAW_SEVEN_DAYS_MS (7.0 * 24.0 * 3600.0 * 1000.0)
#define OPENCLAW_AVG_SESSIONS 5

typedef struct {
	double input;
	double output;
	double cache_read;
	double cache_write;
	double total_tokens;
	double cost;
	int count;
} openclaw_usage;

typedef struct {
	char session_id[256];
	char label[256];
	double updated_at;
} openclaw_session;

typedef struct {
	openclaw_session *v;
	size_t n;
	size_t cap;
} openclaw_session_list;

static void openclaw_metric_families(context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "openclaw_active_sessions", METRIC_TYPE_GAUGE,
		"Total OpenClaw session files across agents.");
	namespace_metric_family_set(NULL, carg, "openclaw_agent_sessions", METRIC_TYPE_GAUGE,
		"OpenClaw session files per agent.");
	namespace_metric_family_set(NULL, carg, "openclaw_agent_state", METRIC_TYPE_GAUGE,
		"Agent state (0=idle, 1=working, 2=thinking, 3=error).");
	namespace_metric_family_set(NULL, carg, "openclaw_agent_last_activity_timestamp_seconds", METRIC_TYPE_GAUGE,
		"Unix timestamp of last agent session file activity.");
	namespace_metric_family_set(NULL, carg, "openclaw_cron_jobs_total", METRIC_TYPE_GAUGE,
		"Total OpenClaw cron jobs.");
	namespace_metric_family_set(NULL, carg, "openclaw_cron_jobs_enabled", METRIC_TYPE_GAUGE,
		"Enabled OpenClaw cron jobs.");
	namespace_metric_family_set(NULL, carg, "openclaw_cron_job_enabled", METRIC_TYPE_GAUGE,
		"OpenClaw cron job enabled (1) or disabled (0).");
	namespace_metric_family_set(NULL, carg, "openclaw_cron_job_last_run_at_seconds", METRIC_TYPE_GAUGE,
		"Unix timestamp of last OpenClaw cron job run.");
	namespace_metric_family_set(NULL, carg, "openclaw_cron_job_next_run_at_seconds", METRIC_TYPE_GAUGE,
		"Unix timestamp of next OpenClaw cron job run.");
	namespace_metric_family_set(NULL, carg, "openclaw_cron_job_consecutive_errors", METRIC_TYPE_GAUGE,
		"Consecutive OpenClaw cron job errors.");
	namespace_metric_family_set(NULL, carg, "openclaw_cron_job_last_duration_seconds", METRIC_TYPE_GAUGE,
		"Last OpenClaw cron job duration in seconds.");
	namespace_metric_family_set(NULL, carg, "openclaw_cron_job_last_delivered", METRIC_TYPE_GAUGE,
		"Last OpenClaw cron job message delivered (1/0).");
	namespace_metric_family_set(NULL, carg, "openclaw_cron_job_created_at_seconds", METRIC_TYPE_GAUGE,
		"OpenClaw cron job creation Unix timestamp.");
	namespace_metric_family_set(NULL, carg, "openclaw_cron_session_tokens_last", METRIC_TYPE_GAUGE,
		"Token usage in last OpenClaw cron session.");
	namespace_metric_family_set(NULL, carg, "openclaw_cron_session_cost_last_usd", METRIC_TYPE_GAUGE,
		"Cost of last OpenClaw cron session in USD.");
	namespace_metric_family_set(NULL, carg, "openclaw_cron_session_total_tokens_last", METRIC_TYPE_GAUGE,
		"Total tokens in last OpenClaw cron session.");
	namespace_metric_family_set(NULL, carg, "openclaw_agent_session_avg_tokens", METRIC_TYPE_GAUGE,
		"Average tokens per OpenClaw session (last 5 non-cron).");
	namespace_metric_family_set(NULL, carg, "openclaw_agent_session_avg_cost_usd", METRIC_TYPE_GAUGE,
		"Average cost per OpenClaw session in USD (last 5 non-cron).");
	namespace_metric_family_set(NULL, carg, "openclaw_agent_session_last_tokens", METRIC_TYPE_GAUGE,
		"Tokens in the latest OpenClaw non-cron session.");
	namespace_metric_family_set(NULL, carg, "openclaw_agent_session_last_cost_usd", METRIC_TYPE_GAUGE,
		"Cost of the latest OpenClaw non-cron session in USD.");
	namespace_metric_family_set(NULL, carg, "openclaw_md_file_bytes", METRIC_TYPE_GAUGE,
		"OpenClaw workspace markdown file size in bytes.");
	namespace_metric_family_set(NULL, carg, "openclaw_md_file_tokens_estimated", METRIC_TYPE_GAUGE,
		"Estimated token count of an OpenClaw workspace markdown file.");
	namespace_metric_family_set(NULL, carg, "openclaw_md_workspace_bytes", METRIC_TYPE_GAUGE,
		"Total markdown bytes in an OpenClaw workspace.");
	namespace_metric_family_set(NULL, carg, "openclaw_md_workspace_tokens_estimated", METRIC_TYPE_GAUGE,
		"Total estimated tokens in an OpenClaw workspace.");
}

static int openclaw_ends_with(const char *s, const char *suf)
{
	size_t n, m;

	if (!s || !suf)
		return 0;
	n = strlen(s);
	m = strlen(suf);
	return n >= m && !strcmp(s + n - m, suf);
}

static int openclaw_uv_stat(const char *path, uv_stat_t *out)
{
	uv_fs_t req;
	int rc;

	memset(&req, 0, sizeof(req));
	rc = uv_fs_stat(NULL, &req, path, NULL);
	if (rc >= 0)
		*out = req.statbuf;
	uv_fs_req_cleanup(&req);
	return rc;
}

static char *openclaw_read_file(const char *path, size_t *out_len)
{
	FILE *fp;
	long sz;
	char *buf;
	size_t n;

	fp = fopen(path, "rb");
	if (!fp)
		return NULL;
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return NULL;
	}
	sz = ftell(fp);
	if (sz < 0) {
		fclose(fp);
		return NULL;
	}
	rewind(fp);
	buf = malloc((size_t)sz + 1);
	if (!buf) {
		fclose(fp);
		return NULL;
	}
	n = fread(buf, 1, (size_t)sz, fp);
	fclose(fp);
	buf[n] = 0;
	if (out_len)
		*out_len = n;
	return buf;
}

static char *openclaw_tail_file(const char *path, size_t nbytes, size_t *out_len)
{
	FILE *fp;
	long sz, start;
	char *buf, *p;
	size_t n, rest;

	fp = fopen(path, "rb");
	if (!fp)
		return NULL;
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return NULL;
	}
	sz = ftell(fp);
	if (sz < 0) {
		fclose(fp);
		return NULL;
	}
	start = 0;
	if ((size_t)sz > nbytes)
		start = sz - (long)nbytes;
	if (fseek(fp, start, SEEK_SET) != 0) {
		fclose(fp);
		return NULL;
	}
	n = (size_t)(sz - start);
	buf = malloc(n + 1);
	if (!buf) {
		fclose(fp);
		return NULL;
	}
	n = fread(buf, 1, n, fp);
	fclose(fp);
	buf[n] = 0;
	p = buf;
	if (start > 0) {
		char *nl = strchr(buf, '\n');
		if (nl)
			p = nl + 1;
	}
	if (p != buf) {
		rest = n - (size_t)(p - buf);
		memmove(buf, p, rest + 1);
		n = rest;
	}
	if (out_len)
		*out_len = n;
	return buf;
}

static const char *openclaw_json_str(json_t *obj, const char *key)
{
	json_t *v = json_object_get(obj, key);
	return json_is_string(v) ? json_string_value(v) : NULL;
}

static int openclaw_json_num(json_t *obj, const char *key, double *out)
{
	json_t *v = json_object_get(obj, key);
	if (!v || json_is_null(v) || !json_is_number(v))
		return 0;
	*out = json_number_value(v);
	return 1;
}

static int openclaw_json_bool(json_t *obj, const char *key)
{
	json_t *v = json_object_get(obj, key);
	return json_is_true(v);
}

static double openclaw_json_cost(json_t *usage)
{
	json_t *cost = json_object_get(usage, "cost");
	json_t *total;

	if (json_is_number(cost))
		return json_number_value(cost);
	if (!json_is_object(cost))
		return 0;
	total = json_object_get(cost, "total");
	return json_is_number(total) ? json_number_value(total) : 0;
}

static json_t *openclaw_usage_obj(json_t *rec)
{
	json_t *usage = json_object_get(rec, "usage");
	json_t *msg;

	if (json_is_object(usage))
		return usage;
	msg = json_object_get(rec, "message");
	if (!json_is_object(msg))
		return NULL;
	usage = json_object_get(msg, "usage");
	return json_is_object(usage) ? usage : NULL;
}

static void openclaw_usage_add(openclaw_usage *totals, json_t *usage)
{
	double n;

	if (openclaw_json_num(usage, "input", &n))
		totals->input += n;
	if (openclaw_json_num(usage, "output", &n))
		totals->output += n;
	if (openclaw_json_num(usage, "cacheRead", &n))
		totals->cache_read += n;
	if (openclaw_json_num(usage, "cacheWrite", &n))
		totals->cache_write += n;
	if (openclaw_json_num(usage, "totalTokens", &n))
		totals->total_tokens += n;
	totals->cost += openclaw_json_cost(usage);
	totals->count++;
}

static int openclaw_read_session_usage(const char *home, const char *agent, const char *session_id, openclaw_usage *out)
{
	char path[OPENCLAW_PATH_MAX];
	char *data, *save, *line;
	json_error_t err;

	memset(out, 0, sizeof(*out));
	snprintf(path, sizeof(path), "%s/agents/%s/sessions/%s.jsonl", home, agent, session_id);
	data = openclaw_tail_file(path, OPENCLAW_TAIL_USAGE, NULL);
	if (!data)
		return 0;
	for (line = strtok_r(data, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		json_t *rec, *usage;

		if (!*line)
			continue;
		rec = json_loads(line, 0, &err);
		if (!rec)
			continue;
		usage = openclaw_usage_obj(rec);
		if (usage)
			openclaw_usage_add(out, usage);
		json_decref(rec);
	}
	free(data);
	return out->count > 0;
}

static int openclaw_session_list_push(openclaw_session_list *list, const char *session_id, const char *label, double updated_at)
{
	openclaw_session *n;

	if (!session_id || !*session_id)
		return 0;
	if (list->n == list->cap) {
		size_t ncap = list->cap ? list->cap * 2 : 16;
		openclaw_session *nv = realloc(list->v, ncap * sizeof(*nv));
		if (!nv)
			return 0;
		list->v = nv;
		list->cap = ncap;
	}
	n = &list->v[list->n++];
	strlcpy(n->session_id, session_id, sizeof(n->session_id));
	strlcpy(n->label, label ? label : "", sizeof(n->label));
	n->updated_at = updated_at;
	return 1;
}

static void openclaw_session_from_obj(json_t *obj, const char *key, openclaw_session_list *list)
{
	const char *sid, *label;
	double updated = 0;

	if (!json_is_object(obj))
		return;
	sid = openclaw_json_str(obj, "sessionId");
	if (!sid)
		sid = key;
	label = openclaw_json_str(obj, "label");
	openclaw_json_num(obj, "updatedAt", &updated);
	openclaw_session_list_push(list, sid, label, updated);
}

static void openclaw_load_sessions(const char *home, const char *agent, openclaw_session_list *list)
{
	char path[OPENCLAW_PATH_MAX];
	char *data;
	json_t *root;
	json_error_t err;

	memset(list, 0, sizeof(*list));
	snprintf(path, sizeof(path), "%s/agents/%s/sessions/sessions.json", home, agent);
	data = openclaw_read_file(path, NULL);
	if (!data)
		return;
	root = json_loads(data, 0, &err);
	free(data);
	if (!root)
		return;
	if (json_is_object(root)) {
		const char *key;
		json_t *val;
		json_object_foreach(root, key, val)
			openclaw_session_from_obj(val, key, list);
	} else if (json_is_array(root)) {
		size_t i, n = json_array_size(root);
		for (i = 0; i < n; i++)
			openclaw_session_from_obj(json_array_get(root, i), NULL, list);
	}
	json_decref(root);
}

static int openclaw_session_cmp_updated_desc(const void *a, const void *b)
{
	const openclaw_session *sa = a, *sb = b;
	if (sa->updated_at > sb->updated_at)
		return -1;
	if (sa->updated_at < sb->updated_at)
		return 1;
	return 0;
}

static void openclaw_cron_name(const char *label, char *out, size_t outsz)
{
	const char *p = strstr(label ? label : "", "Cron:");

	if (!p) {
		strlcpy(out, label ? label : "", outsz);
		return;
	}
	p += 5;
	while (*p == ' ')
		p++;
	strlcpy(out, p, outsz);
}

static int openclaw_name_seen(char names[][256], size_t n, const char *name)
{
	size_t i;
	for (i = 0; i < n; i++) {
		if (!strcmp(names[i], name))
			return 1;
	}
	return 0;
}

static void openclaw_emit_token_types(context_arg *carg, char *metric, const char *agent, const char *cron_name, const openclaw_usage *u)
{
	double v;

	v = u->input;
	if (cron_name)
		metric_add_labels3(metric, &v, DATATYPE_DOUBLE, carg, "agent", (char *)agent, "cron_name", (char *)cron_name, "token_type", "input");
	else
		metric_add_labels2(metric, &v, DATATYPE_DOUBLE, carg, "agent", (char *)agent, "token_type", "input");
	v = u->output;
	if (cron_name)
		metric_add_labels3(metric, &v, DATATYPE_DOUBLE, carg, "agent", (char *)agent, "cron_name", (char *)cron_name, "token_type", "output");
	else
		metric_add_labels2(metric, &v, DATATYPE_DOUBLE, carg, "agent", (char *)agent, "token_type", "output");
	v = u->cache_read;
	if (cron_name)
		metric_add_labels3(metric, &v, DATATYPE_DOUBLE, carg, "agent", (char *)agent, "cron_name", (char *)cron_name, "token_type", "cacheRead");
	else
		metric_add_labels2(metric, &v, DATATYPE_DOUBLE, carg, "agent", (char *)agent, "token_type", "cacheRead");
	v = u->cache_write;
	if (cron_name)
		metric_add_labels3(metric, &v, DATATYPE_DOUBLE, carg, "agent", (char *)agent, "cron_name", (char *)cron_name, "token_type", "cacheWrite");
	else
		metric_add_labels2(metric, &v, DATATYPE_DOUBLE, carg, "agent", (char *)agent, "token_type", "cacheWrite");
}

static int openclaw_content_has_type(json_t *content, const char *type)
{
	size_t i, n;

	if (!json_is_array(content))
		return 0;
	n = json_array_size(content);
	for (i = 0; i < n; i++) {
		json_t *item = json_array_get(content, i);
		const char *t = openclaw_json_str(item, "type");
		if (t && !strcmp(t, type))
			return 1;
	}
	return 0;
}

static void openclaw_agent_state(const char *path, int64_t mtime, int64_t *state, int64_t *timestamp)
{
	char *data, *save;
	char *lines[32];
	int nlines = 0;
	int64_t seconds_ago;
	int i;

	*state = 0;
	*timestamp = mtime;
	data = openclaw_tail_file(path, OPENCLAW_TAIL_STATE, NULL);
	if (!data)
		return;
	for (char *line = strtok_r(data, "\n", &save); line && nlines < 32; line = strtok_r(NULL, "\n", &save)) {
		if (*line)
			lines[nlines++] = line;
	}
	seconds_ago = (int64_t)time(NULL) - mtime;
	for (i = nlines - 1; i >= 0 && i >= nlines - 10; i--) {
		json_error_t err;
		json_t *rec = json_loads(lines[i], 0, &err);
		json_t *msg, *content;
		const char *role;

		if (!rec)
			continue;
		msg = json_object_get(rec, "message");
		content = json_is_object(msg) ? json_object_get(msg, "content") : NULL;
		if (openclaw_content_has_type(content, "toolCall") && seconds_ago < 60) {
			*state = 1;
			json_decref(rec);
			free(data);
			return;
		}
		if (openclaw_content_has_type(content, "thinking") && seconds_ago < 120) {
			*state = 2;
			json_decref(rec);
			free(data);
			return;
		}
		role = json_is_object(msg) ? openclaw_json_str(msg, "role") : NULL;
		if (role && !strcmp(role, "assistant") && seconds_ago < 300) {
			*state = 1;
			json_decref(rec);
			free(data);
			return;
		}
		json_decref(rec);
	}
	free(data);
}

static void openclaw_collect_agent(context_arg *carg, const char *home, const char *agent, uint64_t *total_sessions)
{
	char sessdir[OPENCLAW_PATH_MAX];
	char latest_path[OPENCLAW_PATH_MAX] = "";
	uv_fs_t req;
	uv_dirent_t dirents[256];
	uv_dir_t *rdir;
	uint64_t sessions = 0;
	int64_t latest_mtime_ns = -1;
	int64_t latest_mtime_sec = 0;
	int64_t state = 0, ts = 0;
	openclaw_session_list list;
	openclaw_usage last = {0};
	openclaw_usage sum = {0};
	size_t used = 0, i;
	char seen_cron[64][256];
	size_t nseen = 0;
	double now_ms = (double)time(NULL) * 1000.0;
	double cutoff = now_ms - OPENCLAW_SEVEN_DAYS_MS;

	snprintf(sessdir, sizeof(sessdir), "%s/agents/%s/sessions", home, agent);
	memset(&req, 0, sizeof(req));
	uv_fs_opendir(NULL, &req, sessdir, NULL);
	if (!req.ptr) {
		uv_fs_req_cleanup(&req);
		metric_add_labels("openclaw_agent_sessions", &sessions, DATATYPE_UINT, carg, "agent_name", (char *)agent);
		metric_add_labels("openclaw_agent_state", &state, DATATYPE_INT, carg, "agent_name", (char *)agent);
		metric_add_labels("openclaw_agent_last_activity_timestamp_seconds", &ts, DATATYPE_INT, carg, "agent_name", (char *)agent);
		return;
	}
	rdir = req.ptr;
	rdir->dirents = dirents;
	rdir->nentries = 256;
	for (;;) {
		int n = uv_fs_readdir(NULL, &req, req.ptr, NULL);
		int i_ent;

		if (n <= 0)
			break;
		for (i_ent = 0; i_ent < n; i_ent++) {
			const char *name = dirents[i_ent].name;
			char child[OPENCLAW_PATH_MAX];
			uv_stat_t st;

			if (!name || name[0] == '.' || !openclaw_ends_with(name, ".jsonl")) {
				if (name)
					free((void *)name);
				continue;
			}
			snprintf(child, sizeof(child), "%s/%s", sessdir, name);
			if (openclaw_uv_stat(child, &st) >= 0 && S_ISREG(st.st_mode)) {
				int64_t mtime_ns;

				sessions++;
				mtime_ns = (int64_t)st.st_mtim.tv_sec * 1000000000LL + (int64_t)st.st_mtim.tv_nsec;
				if (mtime_ns >= latest_mtime_ns) {
					latest_mtime_ns = mtime_ns;
					latest_mtime_sec = (int64_t)st.st_mtim.tv_sec;
					strlcpy(latest_path, child, sizeof(latest_path));
				}
			}
			free((void *)name);
		}
	}
	uv_fs_closedir(NULL, &req, req.ptr, NULL);
	uv_fs_req_cleanup(&req);

	if (latest_path[0])
		openclaw_agent_state(latest_path, latest_mtime_sec, &state, &ts);

	*total_sessions += sessions;
	metric_add_labels("openclaw_agent_sessions", &sessions, DATATYPE_UINT, carg, "agent_name", (char *)agent);
	metric_add_labels("openclaw_agent_state", &state, DATATYPE_INT, carg, "agent_name", (char *)agent);
	metric_add_labels("openclaw_agent_last_activity_timestamp_seconds", &ts, DATATYPE_INT, carg, "agent_name", (char *)agent);

	openclaw_load_sessions(home, agent, &list);
	if (list.n)
		qsort(list.v, list.n, sizeof(*list.v), openclaw_session_cmp_updated_desc);

	for (i = 0; i < list.n; i++) {
		openclaw_session *s = &list.v[i];
		char cron_name[256];
		openclaw_usage usage;

		if (!strstr(s->label, "Cron:"))
			continue;
		if (s->updated_at < cutoff)
			continue;
		openclaw_cron_name(s->label, cron_name, sizeof(cron_name));
		if (!cron_name[0] || openclaw_name_seen(seen_cron, nseen, cron_name))
			continue;
		if (nseen < 64)
			strlcpy(seen_cron[nseen++], cron_name, sizeof(seen_cron[0]));
		if (!openclaw_read_session_usage(home, agent, s->session_id, &usage))
			continue;
		openclaw_emit_token_types(carg, "openclaw_cron_session_tokens_last", agent, cron_name, &usage);
		metric_add_labels2("openclaw_cron_session_cost_last_usd", &usage.cost, DATATYPE_DOUBLE, carg, "agent", (char *)agent, "cron_name", cron_name);
		metric_add_labels2("openclaw_cron_session_total_tokens_last", &usage.total_tokens, DATATYPE_DOUBLE, carg, "agent", (char *)agent, "cron_name", cron_name);
	}

	for (i = 0; i < list.n && used < OPENCLAW_AVG_SESSIONS; i++) {
		openclaw_session *s = &list.v[i];
		openclaw_usage usage;

		if (!strncmp(s->label, "Cron", 4))
			continue;
		if (!openclaw_read_session_usage(home, agent, s->session_id, &usage))
			continue;
		if (!used)
			last = usage;
		sum.input += usage.input;
		sum.output += usage.output;
		sum.cache_read += usage.cache_read;
		sum.cache_write += usage.cache_write;
		sum.cost += usage.cost;
		used++;
	}
	if (used) {
		double n = (double)used;
		openclaw_usage avg;

		avg.input = round(sum.input / n);
		avg.output = round(sum.output / n);
		avg.cache_read = round(sum.cache_read / n);
		avg.cache_write = round(sum.cache_write / n);
		avg.cost = sum.cost / n;
		openclaw_emit_token_types(carg, "openclaw_agent_session_last_tokens", agent, NULL, &last);
		metric_add_labels("openclaw_agent_session_last_cost_usd", &last.cost, DATATYPE_DOUBLE, carg, "agent", (char *)agent);
		openclaw_emit_token_types(carg, "openclaw_agent_session_avg_tokens", agent, NULL, &avg);
		metric_add_labels("openclaw_agent_session_avg_cost_usd", &avg.cost, DATATYPE_DOUBLE, carg, "agent", (char *)agent);
	}
	free(list.v);
}

static void openclaw_collect_agents(context_arg *carg, const char *home)
{
	char agentsdir[OPENCLAW_PATH_MAX];
	uv_fs_t req;
	uv_dirent_t dirents[256];
	uv_dir_t *rdir;
	uint64_t total = 0;

	snprintf(agentsdir, sizeof(agentsdir), "%s/agents", home);
	memset(&req, 0, sizeof(req));
	uv_fs_opendir(NULL, &req, agentsdir, NULL);
	if (!req.ptr) {
		uv_fs_req_cleanup(&req);
		metric_add_auto("openclaw_active_sessions", &total, DATATYPE_UINT, carg);
		return;
	}
	rdir = req.ptr;
	rdir->dirents = dirents;
	rdir->nentries = 256;
	for (;;) {
		int n = uv_fs_readdir(NULL, &req, req.ptr, NULL);
		int i;

		if (n <= 0)
			break;
		for (i = 0; i < n; i++) {
			const char *name = dirents[i].name;
			char child[OPENCLAW_PATH_MAX];
			uv_dirent_type_t t = dirents[i].type;
			uv_stat_t st;

			if (!name || name[0] == '.') {
				if (name)
					free((void *)name);
				continue;
			}
			snprintf(child, sizeof(child), "%s/%s", agentsdir, name);
			if (t == UV_DIRENT_UNKNOWN || t == UV_DIRENT_DIR) {
				if (openclaw_uv_stat(child, &st) >= 0 && S_ISDIR(st.st_mode))
					t = UV_DIRENT_DIR;
			}
			if (t == UV_DIRENT_DIR)
				openclaw_collect_agent(carg, home, name, &total);
			free((void *)name);
		}
	}
	uv_fs_closedir(NULL, &req, req.ptr, NULL);
	uv_fs_req_cleanup(&req);
	metric_add_auto("openclaw_active_sessions", &total, DATATYPE_UINT, carg);
}

static void openclaw_collect_cron(context_arg *carg, const char *home)
{
	char path[OPENCLAW_PATH_MAX];
	char *data;
	json_t *root, *jobs;
	json_error_t err;
	size_t i, n;
	uint64_t total = 0, enabled_n = 0;

	snprintf(path, sizeof(path), "%s/cron/jobs.json", home);
	data = openclaw_read_file(path, NULL);
	if (!data) {
		metric_add_auto("openclaw_cron_jobs_total", &total, DATATYPE_UINT, carg);
		metric_add_auto("openclaw_cron_jobs_enabled", &enabled_n, DATATYPE_UINT, carg);
		return;
	}
	root = json_loads(data, 0, &err);
	free(data);
	if (!root) {
		metric_add_auto("openclaw_cron_jobs_total", &total, DATATYPE_UINT, carg);
		metric_add_auto("openclaw_cron_jobs_enabled", &enabled_n, DATATYPE_UINT, carg);
		return;
	}
	jobs = json_object_get(root, "jobs");
	n = json_is_array(jobs) ? json_array_size(jobs) : 0;
	total = n;
	for (i = 0; i < n; i++) {
		json_t *job = json_array_get(jobs, i);
		json_t *state;
		const char *id, *name;
		char job_id[16], job_name[256];
		int64_t enabled, delivered;
		double num;

		if (!json_is_object(job))
			continue;
		id = openclaw_json_str(job, "id");
		name = openclaw_json_str(job, "name");
		strlcpy(job_id, id ? id : "", sizeof(job_id));
		if (strlen(job_id) > 8)
			job_id[8] = 0;
		strlcpy(job_name, name && *name ? name : "Unknown", sizeof(job_name));
		enabled = openclaw_json_bool(job, "enabled") ? 1 : 0;
		if (enabled)
			enabled_n++;
		metric_add_labels2("openclaw_cron_job_enabled", &enabled, DATATYPE_INT, carg, "job_name", job_name, "job_id", job_id);

		state = json_object_get(job, "state");
		if (json_is_object(state)) {
			if (openclaw_json_num(state, "lastRunAtMs", &num)) {
				num /= 1000.0;
				metric_add_labels2("openclaw_cron_job_last_run_at_seconds", &num, DATATYPE_DOUBLE, carg, "job_name", job_name, "job_id", job_id);
			}
			if (openclaw_json_num(state, "nextRunAtMs", &num)) {
				num /= 1000.0;
				metric_add_labels2("openclaw_cron_job_next_run_at_seconds", &num, DATATYPE_DOUBLE, carg, "job_name", job_name, "job_id", job_id);
			}
			if (openclaw_json_num(state, "consecutiveErrors", &num))
				metric_add_labels2("openclaw_cron_job_consecutive_errors", &num, DATATYPE_DOUBLE, carg, "job_name", job_name, "job_id", job_id);
			else {
				num = 0;
				metric_add_labels2("openclaw_cron_job_consecutive_errors", &num, DATATYPE_DOUBLE, carg, "job_name", job_name, "job_id", job_id);
			}
			if (openclaw_json_num(state, "lastDurationMs", &num)) {
				num /= 1000.0;
				metric_add_labels2("openclaw_cron_job_last_duration_seconds", &num, DATATYPE_DOUBLE, carg, "job_name", job_name, "job_id", job_id);
			}
			delivered = openclaw_json_bool(state, "lastDelivered") ? 1 : 0;
			metric_add_labels2("openclaw_cron_job_last_delivered", &delivered, DATATYPE_INT, carg, "job_name", job_name, "job_id", job_id);
		}
		if (openclaw_json_num(job, "createdAtMs", &num) && num > 0) {
			num /= 1000.0;
			metric_add_labels2("openclaw_cron_job_created_at_seconds", &num, DATATYPE_DOUBLE, carg, "job_name", job_name, "job_id", job_id);
		}
	}
	metric_add_auto("openclaw_cron_jobs_total", &total, DATATYPE_UINT, carg);
	metric_add_auto("openclaw_cron_jobs_enabled", &enabled_n, DATATYPE_UINT, carg);
	json_decref(root);
}

static void openclaw_collect_md_dir(context_arg *carg, const char *ws_path, const char *rel_prefix, const char *workspace, uint64_t *total_bytes, double *total_tokens)
{
	uv_fs_t req;
	uv_dirent_t dirents[256];
	uv_dir_t *rdir;

	memset(&req, 0, sizeof(req));
	uv_fs_opendir(NULL, &req, ws_path, NULL);
	if (!req.ptr) {
		uv_fs_req_cleanup(&req);
		return;
	}
	rdir = req.ptr;
	rdir->dirents = dirents;
	rdir->nentries = 256;
	for (;;) {
		int n = uv_fs_readdir(NULL, &req, req.ptr, NULL);
		int i;

		if (n <= 0)
			break;
		for (i = 0; i < n; i++) {
			const char *name = dirents[i].name;
			char child[OPENCLAW_PATH_MAX];
			char rel[512];
			uv_stat_t st;
			double size, tokens;

			if (!name || name[0] == '.' || !openclaw_ends_with(name, ".md")) {
				if (name)
					free((void *)name);
				continue;
			}
			snprintf(child, sizeof(child), "%s/%s", ws_path, name);
			if (openclaw_uv_stat(child, &st) < 0 || !S_ISREG(st.st_mode)) {
				free((void *)name);
				continue;
			}
			if (rel_prefix && *rel_prefix)
				snprintf(rel, sizeof(rel), "%s/%s", rel_prefix, name);
			else
				strlcpy(rel, name, sizeof(rel));
			size = (double)st.st_size;
			tokens = round(size / 3.5);
			metric_add_labels2("openclaw_md_file_bytes", &size, DATATYPE_DOUBLE, carg, "workspace", (char *)workspace, "filename", rel);
			metric_add_labels2("openclaw_md_file_tokens_estimated", &tokens, DATATYPE_DOUBLE, carg, "workspace", (char *)workspace, "filename", rel);
			*total_bytes += (uint64_t)st.st_size;
			*total_tokens += tokens;
			free((void *)name);
		}
	}
	uv_fs_closedir(NULL, &req, req.ptr, NULL);
	uv_fs_req_cleanup(&req);
}

static void openclaw_workspace_label(const char *dirname, char *out, size_t outsz)
{
	if (!strcmp(dirname, "workspace")) {
		strlcpy(out, "main", outsz);
		return;
	}
	if (!strncmp(dirname, "workspace-", 10) && dirname[10]) {
		strlcpy(out, dirname + 10, outsz);
		return;
	}
	strlcpy(out, dirname, outsz);
}

static void openclaw_collect_workspaces(context_arg *carg, const char *home)
{
	uv_fs_t req;
	uv_dirent_t dirents[256];
	uv_dir_t *rdir;

	memset(&req, 0, sizeof(req));
	uv_fs_opendir(NULL, &req, home, NULL);
	if (!req.ptr) {
		uv_fs_req_cleanup(&req);
		return;
	}
	rdir = req.ptr;
	rdir->dirents = dirents;
	rdir->nentries = 256;
	for (;;) {
		int n = uv_fs_readdir(NULL, &req, req.ptr, NULL);
		int i;

		if (n <= 0)
			break;
		for (i = 0; i < n; i++) {
			const char *name = dirents[i].name;
			char wspath[OPENCLAW_PATH_MAX];
			char mempath[OPENCLAW_PATH_MAX];
			char label[256];
			uv_dirent_type_t t = dirents[i].type;
			uv_stat_t st;
			uint64_t bytes = 0;
			double tokens = 0;
			double dbytes;

			if (!name || name[0] == '.' || strncmp(name, "workspace", 9) != 0) {
				if (name)
					free((void *)name);
				continue;
			}
			snprintf(wspath, sizeof(wspath), "%s/%s", home, name);
			if (t == UV_DIRENT_UNKNOWN || t == UV_DIRENT_DIR) {
				if (openclaw_uv_stat(wspath, &st) >= 0 && S_ISDIR(st.st_mode))
					t = UV_DIRENT_DIR;
			}
			if (t != UV_DIRENT_DIR) {
				free((void *)name);
				continue;
			}
			openclaw_workspace_label(name, label, sizeof(label));
			openclaw_collect_md_dir(carg, wspath, NULL, label, &bytes, &tokens);
			snprintf(mempath, sizeof(mempath), "%s/memory", wspath);
			openclaw_collect_md_dir(carg, mempath, "memory", label, &bytes, &tokens);
			dbytes = (double)bytes;
			metric_add_labels("openclaw_md_workspace_bytes", &dbytes, DATATYPE_DOUBLE, carg, "workspace", label);
			metric_add_labels("openclaw_md_workspace_tokens_estimated", &tokens, DATATYPE_DOUBLE, carg, "workspace", label);
			free((void *)name);
		}
	}
	uv_fs_closedir(NULL, &req, req.ptr, NULL);
	uv_fs_req_cleanup(&req);
}

static int openclaw_resolve_root(char *root, size_t root_sz, char *metrics, size_t size, context_arg *carg)
{
	const char *home;
	size_t rlen;

	root[0] = 0;
	if (carg->host[0] == '/')
		strlcpy(root, carg->host, root_sz);
	else if (carg->query_url && carg->query_url[0] == '/')
		strlcpy(root, carg->query_url, root_sz);
	else if (metrics && size && metrics[0] == '/') {
		size_t n = size < root_sz - 1 ? size : root_sz - 1;
		memcpy(root, metrics, n);
		root[n] = 0;
		root[strcspn(root, "\r\n")] = 0;
	} else if (carg->host[0] == '~' || (metrics && size && metrics[0] == '~')) {
		strlcpy(root, carg->host[0] ? carg->host : metrics, root_sz);
	}

	if (!strncmp(root, "file://", 7))
		memmove(root, root + 7, strlen(root + 7) + 1);

	if (root[0] == '~' && (root[1] == '/' || root[1] == 0)) {
		home = getenv("HOME");
		if (home && *home) {
			char rest[OPENCLAW_PATH_MAX];
			strlcpy(rest, root[1] ? root + 1 : "", sizeof(rest));
			snprintf(root, root_sz, "%s%s", home, rest);
		}
	}

	rlen = strlen(root);
	while (rlen > 1 && root[rlen - 1] == '/')
		root[--rlen] = 0;
	return root[0] != 0;
}

void openclaw_handler(char *metrics, size_t size, context_arg *carg)
{
	char root[OPENCLAW_PATH_MAX];

	openclaw_metric_families(carg);
	if (!openclaw_resolve_root(root, sizeof(root), metrics, size, carg)) {
		carg->parser_status = 0;
		return;
	}
	carglog(carg, L_DEBUG, "openclaw home '%s'\n", root);
	openclaw_collect_agents(carg, root);
	openclaw_collect_cron(carg, root);
	openclaw_collect_workspaces(carg, root);
	carg->parser_status = 1;
}

string* openclaw_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	(void)arg;
	(void)env;
	(void)proxy_settings;
	(void)hi;
	return NULL;
}

void openclaw_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("openclaw");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler) * actx->handlers);
	actx->handler[0].name = openclaw_handler;
	actx->handler[0].mesg_func = openclaw_mesg;
	strlcpy(actx->handler[0].key, "openclaw", 255);
	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
