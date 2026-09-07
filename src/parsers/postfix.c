#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "events/context_arg.h"
#include "common/selector.h"
#include "main.h"

static int postfix_uv_stat(const char *path, uv_stat_t *out)
{
	uv_fs_t req;
	memset(&req, 0, sizeof(req));
	int rc = uv_fs_stat(NULL, &req, path, NULL);
	if (rc >= 0)
		*out = req.statbuf;
	uv_fs_req_cleanup(&req);
	return rc;
}

static void postfix_account(const uv_stat_t *st, uint64_t now, uint64_t *count, uint64_t *bytes, int64_t *oldest)
{
	++*count;
	*bytes += (uint64_t)st->st_size;
	int64_t age = (int64_t)now - (int64_t)st->st_mtim.tv_sec;
	if (age < 0)
		age = 0;
	if (age > *oldest)
		*oldest = age;
}

static void postfix_walk_dir(const char *path, int depth, uint64_t now,
	uint64_t *count, uint64_t *bytes, int64_t *oldest)
{
	uv_fs_t req;
	memset(&req, 0, sizeof(req));
	uv_fs_opendir(NULL, &req, path, NULL);
	if (!req.ptr) {
		uv_fs_req_cleanup(&req);
		return;
	}

	uv_dirent_t dirents[256];
	uv_dir_t *rdir = req.ptr;
	rdir->dirents = dirents;
	rdir->nentries = 256;

	for (;;) {
		int n = uv_fs_readdir(NULL, &req, req.ptr, NULL);
		if (n <= 0)
			break;
		for (int i = 0; i < n; i++) {
			const char *name = dirents[i].name;
			if (name && name[0] != '.') {
				char child[1600];
				snprintf(child, sizeof(child), "%s/%s", path, name);
				uv_dirent_type_t t = dirents[i].type;
				uv_stat_t st;
				int have_stat = 0;
				if (t == UV_DIRENT_UNKNOWN || t == UV_DIRENT_DIR) {
					if (postfix_uv_stat(child, &st) >= 0) {
						have_stat = 1;
						if (S_ISDIR(st.st_mode))
							t = UV_DIRENT_DIR;
						else if (S_ISREG(st.st_mode))
							t = UV_DIRENT_FILE;
					}
				}
				if (t == UV_DIRENT_DIR && depth < 1)
					postfix_walk_dir(child, depth + 1, now, count, bytes, oldest);
				else if (t == UV_DIRENT_FILE) {
					if (!have_stat)
						have_stat = postfix_uv_stat(child, &st) >= 0;
					if (have_stat)
						postfix_account(&st, now, count, bytes, oldest);
				}
			}
			if (dirents[i].name)
				free((void *)dirents[i].name);
		}
	}

	uv_fs_closedir(NULL, &req, req.ptr, NULL);
	uv_fs_req_cleanup(&req);
}

static void postfix_walk_queue(const char *root, char *queue, time_t now, context_arg *carg)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", root, queue);

	uint64_t count = 0;
	uint64_t bytes = 0;
	int64_t oldest = 0;
	postfix_walk_dir(path, 0, (uint64_t)now, &count, &bytes, &oldest);

	metric_add_labels("postfix_queue_length", &count, DATATYPE_UINT, carg, "queue", queue);
	metric_add_labels("postfix_queue_size_bytes", &bytes, DATATYPE_UINT, carg, "queue", queue);
	metric_add_labels("postfix_queue_oldest_seconds", &oldest, DATATYPE_INT, carg, "queue", queue);
}

void postfix_handler(char *metrics, size_t size, context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "postfix_queue_length", METRIC_TYPE_GAUGE, "Postfix queue file count.");
	namespace_metric_family_set(NULL, carg, "postfix_queue_size_bytes", METRIC_TYPE_GAUGE, "Postfix queue total size in bytes.");
	namespace_metric_family_set(NULL, carg, "postfix_queue_oldest_seconds", METRIC_TYPE_GAUGE, "Age of oldest file in a Postfix queue.");

	char root[1024] = "";
	if (carg->host[0] == '/')
		strlcpy(root, carg->host, sizeof(root));
	else if (carg->query_url && carg->query_url[0] == '/')
		strlcpy(root, carg->query_url, sizeof(root));
	else if (metrics && size && metrics[0] == '/') {
		size_t n = size < sizeof(root) - 1 ? size : sizeof(root) - 1;
		memcpy(root, metrics, n);
		root[n] = '\0';
		root[strcspn(root, "\r\n")] = 0;
	}
	if (!strncmp(root, "file://", 7))
		memmove(root, root + 7, strlen(root + 7) + 1);
	size_t rlen = strlen(root);
	while (rlen > 1 && root[rlen - 1] == '/')
		root[--rlen] = '\0';
	if (!root[0]) {
		carg->parser_status = 0;
		return;
	}

	time_t now = time(NULL);
	static char *queues[] = { "active", "hold", "incoming", "deferred", "maildrop" };
	for (size_t i = 0; i < sizeof(queues) / sizeof(queues[0]); ++i)
		postfix_walk_queue(root, queues[i], now, carg);
	carg->parser_status = 1;
}

string* postfix_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	(void)arg;
	(void)env;
	(void)proxy_settings;
	(void)hi;
	return NULL;
}

void postfix_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));
	actx->key = strdup("postfix");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler) * actx->handlers);
	actx->handler[0].name = postfix_handler;
	actx->handler[0].mesg_func = postfix_mesg;
	strlcpy(actx->handler[0].key, "postfix", 255);
	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
