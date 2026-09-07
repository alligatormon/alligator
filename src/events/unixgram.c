#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <uv.h>
#include "cluster/later.h"
#include "parsers/multiparser.h"
#include "common/entrypoint.h"
#include "common/logs.h"
#include "events/context_arg.h"
#include "events/metrics.h"
#include "common/rtime.h"
#include "dstructures/uv_cache.h"
#include "common/aggregator.h"
#include "main.h"

extern aconf* ac;

void unixgram_client_repeat_period(uv_timer_t *timer);
static void unixgram_timeout_timer(uv_timer_t *timer);
static void unixgram_close_client(context_arg *carg);
static void unixgram_session_finish(context_arg *carg);

static void unixgram_cleanup_addrs(context_arg *carg)
{
	if (carg->local && carg->local->sun_path[0]) {
		carglog(carg, L_DEBUG, "unixgram: deleting socket file %s\n", carg->local->sun_path);
		unlink(carg->local->sun_path);
	}
	free(carg->remote);
	free(carg->local);
	carg->remote = NULL;
	carg->local = NULL;
	if (carg->fd >= 0) {
		close(carg->fd);
		carg->fd = -1;
	}
}

static void unixgram_session_finish(context_arg *carg)
{
	(carg->close_counter)++;
	carg->lock = 0;

	aggregator_events_metric_add(carg, carg, NULL, "unixgram", "aggregator", carg->host);

	if (carg->context_ttl)
	{
		r_time time = setrtime();
		if (time.sec >= carg->context_ttl)
		{
			carg->remove_from_hash = 1;
			smart_aggregator_del(carg);
		}
	}
	else if (carg->period && carg->period_timer) {
		uv_timer_stop(carg->period_timer);
		uv_timer_start(carg->period_timer, unixgram_client_repeat_period, carg->period, 0);
	}
}

static void unixgram_poll_closed(uv_handle_t *handle)
{
	context_arg *carg = handle->data;
	unixgram_cleanup_addrs(carg);
	unixgram_session_finish(carg);
}

static void unixgram_stop_timeout(context_arg *carg)
{
	if (!carg->tt_timer)
		return;
	uv_timer_stop(carg->tt_timer);
	carg->tt_timer->data = NULL;
	alligator_cache_push(ac->uv_cache_timer, carg->tt_timer);
	carg->tt_timer = NULL;
}

static void unixgram_close_client(context_arg *carg)
{
	unixgram_stop_timeout(carg);

	if (carg->poll_socket.type == UV_POLL && !uv_is_closing((uv_handle_t *)&carg->poll_socket)) {
		uv_poll_stop(&carg->poll_socket);
		uv_close((uv_handle_t *)&carg->poll_socket, unixgram_poll_closed);
		return;
	}

	unixgram_cleanup_addrs(carg);
	unixgram_session_finish(carg);
}

void unixgram_cb(uv_poll_t* handle, int status, int events)
{
	context_arg *carg = handle->data;
	char buf[65535];
	struct sockaddr_un from;
	socklen_t from_len = sizeof(from);
	int size;

	(void)events;
	if (status)
	{
		carglog(carg, L_ERROR, "unixgram poll error: %s\n", uv_strerror(status));
		unixgram_close_client(carg);
		return;
	}

	if ((size = recvfrom(carg->fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len)) <= 0)
	{
		carglog(carg, L_DEBUG, "recvfrom: %s\n", strerror(errno));
		unixgram_close_client(carg);
		return;
	}

	carg->read_time_finish = setrtime();
	(carg->conn_counter)++;
	(carg->read_counter)++;
	carg->read_bytes_counter += (uint64_t)size;
	alligator_multiparser(buf, size, carg->parser_handler, NULL, carg);
	unixgram_close_client(carg);
}

static int unixgram_bind_client(int s, struct sockaddr_un *local, const char *remote_path, uint64_t id)
{
	char dir[sizeof(local->sun_path)];
	char *slash;
	int len;

	local->sun_family = AF_UNIX;
	if (remote_path && remote_path[0]) {
		strlcpy(dir, remote_path, sizeof(dir));
		slash = strrchr(dir, '/');
		if (slash && slash != dir) {
			*slash = '\0';
			snprintf(local->sun_path, sizeof(local->sun_path), "%s/alligator_%"u64".sock", dir, id);
			unlink(local->sun_path);
			len = (int)(strlen(local->sun_path) + sizeof(local->sun_family));
			if (bind(s, (struct sockaddr *)local, len) == 0)
				return 0;
		}
	}

	snprintf(local->sun_path, sizeof(local->sun_path), "/tmp/alligator_%"u64".sock", id);
	unlink(local->sun_path);
	len = (int)(strlen(local->sun_path) + sizeof(local->sun_family));
	return bind(s, (struct sockaddr *)local, len);
}

void unixgram_client_connect(void *arg)
{
	if (!arg)
		return;

	context_arg *carg = arg;
	size_t send_len;
	int s;

	carglog(carg, L_DEBUG, "unixgram: connecting key=%s path=%s timeout_ms=%"u64"\n",
		carg->key ? carg->key : "-", carg->host, carg->timeout);

	if (carg->lock)
		return;
	if (cluster_come_later(carg))
		return;

	if (carg->period && !carg->close_counter) {
		carg->period_timer = alligator_cache_get(ac->uv_cache_timer, sizeof(uv_timer_t));
		carg->period_timer->data = carg;
		uv_timer_init(carg->loop, carg->period_timer);
		uv_timer_start(carg->period_timer, unixgram_client_repeat_period, carg->period, 0);
	}

	carg->lock = 1;
	carg->parsed = 0;
	carg->parser_status = 0;
	carg->curr_ttl = carg->ttl;
	carg->fd = -1;

	if (!carg->mesg || !carg->mesg_len) {
		carglog(carg, L_ERROR, "unixgram: empty request path=%s\n", carg->host);
		carg->lock = 0;
		return;
	}

	if (!carg->loop)
		carg->loop = uv_default_loop();

	socklen_t remote_len = sizeof(struct sockaddr_un);
	struct sockaddr_un *remote = calloc(1, remote_len);
	remote->sun_family = AF_UNIX;
	strlcpy(remote->sun_path, carg->host, sizeof(remote->sun_path));
	carg->remote_len = remote_len;
	carg->remote = remote;

	struct sockaddr_un *local = calloc(1, sizeof(*local));
	carg->local_len = sizeof(*local);
	carg->local = local;

	if ((s = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) {
		carglog(carg, L_ERROR, "unixgram socket: %s\n", strerror(errno));
		unixgram_cleanup_addrs(carg);
		carg->lock = 0;
		return;
	}
	carg->fd = s;

	ac->request_cnt++;
	if (unixgram_bind_client(s, local, carg->host, ac->request_cnt) == -1)
	{
		carglog(carg, L_ERROR, "unixgram bind %s: %s\n", local->sun_path, strerror(errno));
		unixgram_cleanup_addrs(carg);
		carg->lock = 0;
		return;
	}

	send_len = carg->mesg_len;
	if (sendto(s, carg->mesg, send_len, 0, (struct sockaddr *)remote, remote_len) == -1)
	{
		carglog(carg, L_ERROR, "unixgram sendto %s: %s\n", carg->host, strerror(errno));
		unixgram_cleanup_addrs(carg);
		carg->lock = 0;
		return;
	}

	carg->write_time_finish = setrtime();
	carglog(carg, L_DEBUG, "unixgram: sent %zu bytes path=%s local=%s\n",
		send_len, carg->host, local->sun_path);

	carg->tt_timer = alligator_cache_get(ac->uv_cache_timer, sizeof(uv_timer_t));
	carg->tt_timer->data = carg;
	uv_timer_init(carg->loop, carg->tt_timer);
	uv_timer_start(carg->tt_timer, unixgram_timeout_timer, carg->timeout, 0);

	carg->poll_socket.data = carg;
	uv_poll_init_socket(carg->loop, &carg->poll_socket, s);
	uv_poll_start(&carg->poll_socket, UV_READABLE, unixgram_cb);
	carg->read_time = setrtime();
}

static void unixgram_timeout_timer(uv_timer_t *timer)
{
	context_arg *carg = timer->data;

	uv_timer_stop(timer);
	if (carg)
		carg->tt_timer = NULL;
	alligator_cache_push(ac->uv_cache_timer, timer);

	if (!carg)
		return;

	carglog(carg, L_WARN, "unixgram: timeout key=%s path=%s timeout_ms=%"u64"\n",
		carg->key ? carg->key : "-", carg->host, carg->timeout);
	(carg->timeout_counter)++;
	unixgram_close_client(carg);
}

void unixgram_client_repeat_period(uv_timer_t *timer)
{
	context_arg *carg = timer->data;
	if (!carg->period)
		return;

	unixgram_client_connect((void *)carg);
}

void for_unixgram_client_connect(void *arg)
{
	context_arg *carg = arg;
	if (!carg || carg->remove_from_hash)
		return;

	if (carg->context_ttl) {
		if (carg->lock || carg->close_counter)
			return;
		unixgram_client_connect(arg);
		return;
	}

	if (carg->period && carg->close_counter)
		return;

	unixgram_client_connect(arg);
}

char *unixgram_client(void *arg)
{
	if (!arg)
		return NULL;

	context_arg *carg = arg;
	alligator_ht_insert(ac->unixgram_aggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));
	return "unixgram";
}

void unixgram_client_del(context_arg *carg)
{
	if (!carg)
		return;

	if (carg->lock)
	{
		r_time time = setrtime();
		carg->context_ttl = time.sec;
		unixgram_close_client(carg);
		return;
	}

	carg->lock = 1;

	if (carg->remove_from_hash)
		alligator_ht_remove_existing(ac->aggregators, &(carg->context_node));

	alligator_ht_remove_existing(ac->unixgram_aggregator, &(carg->node));
	carg_free(carg);
}

static void udgregator_timer_cb(uv_timer_t* handle)
{
	(void)handle;
	alligator_ht_foreach(ac->unixgram_aggregator, for_unixgram_client_connect);
}

void unixgram_client_handler()
{
	uv_loop_t *loop = ac->loop;

	uv_timer_init(loop, &ac->udgregator_timer);
	uv_timer_start(&ac->udgregator_timer, udgregator_timer_cb, ac->unixgram_aggregator_startup, ac->unixgram_aggregator_repeat);
}

// server
void unixgram_serve_cb(uv_poll_t* handle, int status, int events)
{
	context_arg *carg = handle->data;
	if (status)
	{
		carglog(carg, L_ERROR, "uv_write error: %s\n", uv_strerror(status));
		return;
	}
	char buf[65535];
	int size;
	if ( ( size = recvfrom(carg->fd, buf, 65535, 0, (struct sockaddr*)carg->local, &carg->local_len)) <= 0 )
	{
		carglog(carg, L_ERROR, "recvfrom: %s\n", strerror(errno));
		return;
	}
	buf[size]=0;
	alligator_multiparser(buf, size, carg->parser_handler, NULL, NULL);
}

static void unixgram_server_poll_closed(uv_handle_t *handle)
{
	context_arg *carg = handle->data;
	carg_free(carg);
}

void unixgram_server_init(uv_loop_t *loop, char *addr, context_arg *carg)
{
	entrypoint_carg_replace_key(carg, "unixgram:%s", addr);

	int s = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (s == -1)
	{
		carglog(carg, L_ERROR, "socket: %s\n", strerror(errno));
		return;
	}
	size_t local_len = sizeof(struct sockaddr_un);
	struct sockaddr_un *local = calloc(1, local_len);
	local->sun_family = AF_UNIX;
	strlcpy(local->sun_path, addr, sizeof(local->sun_path));
	unlink(local->sun_path);
	if (bind(s, (struct sockaddr *)local, local_len) == -1)
	{
		carglog(carg, L_ERROR, "bind: %s\n", strerror(errno));
		return;
	}
	carg->remote = 0;
	carg->local = 0;
	carg->fd = s;

	carg->poll_socket.data = carg;
	uv_poll_init_socket(loop, &carg->poll_socket, s);
	uv_poll_start(&carg->poll_socket, UV_READABLE, unixgram_serve_cb);

	alligator_ht_insert(ac->entrypoints, &(carg->context_node), carg, tommy_strhash_u32(0, carg->key));
}

void unixgram_server_stop(const char* addr)
{
	char key[255];
	snprintf(key, 255, "unixgram:%s", addr);
	context_arg *carg = alligator_ht_search(ac->entrypoints, entrypoint_compare, key, tommy_strhash_u32(0, key));
	if (carg)
	{
		alligator_ht_remove_existing(ac->entrypoints, &(carg->context_node));
		uv_poll_stop(&carg->poll_socket);
		unlink(addr);
		if (!uv_is_closing((uv_handle_t *)&carg->poll_socket))
			uv_close((uv_handle_t *)&carg->poll_socket, unixgram_server_poll_closed);
		else
			carg_free(carg);
	}
}
