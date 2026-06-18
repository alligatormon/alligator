#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <uv.h>
#include "dstructures/tommyds/tommyds/tommy.h"
#include "main.h"
#include "common/selector.h"
#include "events/uv_alloc.h"
#include "cluster/later.h"
#include "parsers/multiparser.h"
#include "common/logs.h"
extern aconf* ac;

static const char *process_shell_path(void)
{
	if (ac->process_shell && ac->process_shell[0])
		return ac->process_shell;
	return "/bin/sh";
}

#define PROCESS_RELEASE_STDIN   (1u << 0)
#define PROCESS_RELEASE_CHANNEL (1u << 1)
#define PROCESS_RELEASE_CHILD   (1u << 2)
#define PROCESS_RELEASE_ALL     (PROCESS_RELEASE_STDIN | PROCESS_RELEASE_CHANNEL | PROCESS_RELEASE_CHILD)

static void process_finalize(context_arg *carg);
static void process_mark_released(context_arg *carg, uint8_t bit);
static void process_try_release(context_arg *carg);
static void process_uv_handle_closed(uv_handle_t *handle);
static void process_close_or_released(context_arg *carg, uv_handle_t *handle, uint8_t bit);
static void process_spawn_failed(context_arg *carg, uv_pipe_t *stdin_pipe, uv_pipe_t *channel);
static void process_deferred_finalize_closed(uv_handle_t *handle);
static void process_deferred_finalize(uv_timer_t *timer);
void process_client_del(context_arg *carg);

static const char *process_parser_name(context_arg *carg)
{
	if (carg && carg->parser_name && carg->parser_name[0])
		return carg->parser_name;
	return "-";
}

static size_t process_body_line_count(const char *s, size_t len)
{
	size_t lines = 0;

	if (!s || !len)
		return 0;

	for (size_t i = 0; i < len; i++)
		if (s[i] == '\n')
			lines++;

	return lines;
}

static uint64_t process_wait_ms(context_arg *carg)
{
	if (!carg)
		return 0;

	r_time now = setrtime();
	return getrtime_mcs(carg->read_time, now, 0) / 1000;
}

static void process_log_body_snapshot(context_arg *carg, const char *stage)
{
	size_t body_len = (carg && carg->full_body) ? carg->full_body->l : 0;
	size_t body_cap = (carg && carg->full_body) ? carg->full_body->m : 0;
	size_t lines = (carg && carg->full_body) ? process_body_line_count(carg->full_body->s, body_len) : 0;

	carglog(carg, L_INFO,
		"process %s key=%s parser=%s pid=%d reads=%" PRIu64 " body=%zu/%zu lines=%zu parsed=%d exit_parsed=%d body_at_exit=%zu\n",
		stage,
		carg && carg->key ? carg->key : "-",
		process_parser_name(carg),
		(carg && carg->child_req.pid) ? carg->child_req.pid : 0,
		carg ? carg->read_counter : 0,
		body_len,
		body_cap,
		lines,
		carg ? carg->parsed : 0,
		carg ? carg->process_exit_parsed : 0,
		carg ? carg->process_body_at_exit : 0);
}

static void process_finalize(context_arg *carg)
{
	carglog(carg, L_INFO, "run process finalize %p with cmd %s\n", carg, carg->host);

	if (carg->context_ttl)
	{
		r_time time = setrtime();
		if (time.sec >= carg->context_ttl)
		{
			carg->remove_from_hash = 1;
			smart_aggregator_del(carg);
			return;
		}
	}

	carg->process_release_scheduled = 0;
	carg->process_released = 0;

	/* Deferred external delete request while process was active. */
	if (carg->remove_from_hash)
	{
		process_client_del(carg);
		return;
	}
}

static void process_mark_released(context_arg *carg, uint8_t bit)
{
	if (!carg)
		return;

	carg->process_released |= bit;
	process_try_release(carg);
}

static void process_try_release(context_arg *carg)
{
	if (!carg || !carg->process_release_scheduled)
		return;
	if ((carg->process_released & PROCESS_RELEASE_ALL) != PROCESS_RELEASE_ALL)
		return;

	carg->process_release_scheduled = 0;

	if (!carg->loop)
	{
		process_finalize(carg);
		return;
	}

	{
		uv_timer_t *defer = malloc(sizeof(*defer));

		if (!defer)
		{
			process_finalize(carg);
			return;
		}

		uv_timer_init(carg->loop, defer);
		defer->data = carg;
		uv_timer_start(defer, process_deferred_finalize, 0, 0);
	}
}

static void process_deferred_finalize(uv_timer_t *timer)
{
	/* Closing-handle callbacks run after pending callbacks. */
	uv_close((uv_handle_t *)timer, process_deferred_finalize_closed);
}

static void process_deferred_finalize_closed(uv_handle_t *handle)
{
	context_arg *carg = handle ? handle->data : NULL;

	free(handle);
	if (carg)
		process_finalize(carg);
}

static void process_uv_handle_closed(uv_handle_t *handle)
{
	context_arg *carg = handle->data;

	if (!carg)
		return;

	if (handle == (uv_handle_t *)&carg->child_stdin)
		process_mark_released(carg, PROCESS_RELEASE_STDIN);
	else if (handle == (uv_handle_t *)&carg->channel)
		process_mark_released(carg, PROCESS_RELEASE_CHANNEL);
	else if (handle == (uv_handle_t *)&carg->child_req)
		process_mark_released(carg, PROCESS_RELEASE_CHILD);
}

static void process_close_or_released(context_arg *carg, uv_handle_t *handle, uint8_t bit)
{
	if (!carg || !handle)
		return;

	if (carg->process_released & bit)
		return;

	if (uv_is_closing(handle))
		return;

	if (!handle->loop)
	{
		process_mark_released(carg, bit);
		return;
	}

	uv_close(handle, process_uv_handle_closed);
}

static void process_spawn_failed(context_arg *carg, uv_pipe_t *stdin_pipe, uv_pipe_t *channel)
{
	if (!carg)
		return;

	carglog(carg, L_WARN, "process spawn failed key=%s parser=%s host=%s\n",
		carg->key ? carg->key : "-", process_parser_name(carg), carg->host ? carg->host : "-");

	carg->lock = 0;
	carg->process_release_scheduled = 1;
	carg->process_released = PROCESS_RELEASE_CHILD;

	process_close_or_released(carg, (uv_handle_t *)stdin_pipe, PROCESS_RELEASE_STDIN);
	process_close_or_released(carg, (uv_handle_t *)channel, PROCESS_RELEASE_CHANNEL);
	process_try_release(carg);
}

static void process_parse_stdout(context_arg *carg, const char *when)
{
	if (!carg || carg->parsed || !carg->parser_handler)
		return;
	if (!carg->full_body || !carg->full_body->l)
		return;

	process_log_body_snapshot(carg, when);
	alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);
}

void echo_read(uv_stream_t *server, ssize_t nread, const uv_buf_t* buf)
{
	context_arg *carg = server->data;
	carglog(carg, L_DEBUG, "Process %p with pid %d with cmd %s read %zd bytes\n", &carg->child_req, carg->child_req.pid, carg->host, nread);

	if (nread == UV_EOF)
	{
		carglog(carg, L_INFO,
			"process lifecycle stop key=%s parser=%s pid=%d reason=stdout_eof wait_ms=%" PRIu64 " reads=%" PRIu64 " body=%zu\n",
			carg->key ? carg->key : "-",
			process_parser_name(carg),
			carg->child_req.pid,
			process_wait_ms(carg),
			carg->read_counter,
			carg->full_body ? carg->full_body->l : 0);
		process_log_body_snapshot(carg, "stdout EOF");
		process_parse_stdout(carg, "stdout EOF parse");
		free_buffer(carg, buf);
		return;
	}

	if (nread < 0)
	{
		carglog(carg, L_WARN, "process stdout read error key=%s parser=%s pid=%d nread=%zd err=%s\n",
			carg->key ? carg->key : "-",
			process_parser_name(carg),
			carg->child_req.pid,
			nread,
			uv_strerror((int)nread));
		free_buffer(carg, buf);
		return;
	}

	(carg->read_counter)++;

	string_cat(carg->full_body, buf->base, nread);

	if (carg->process_exit_parsed)
	{
		carglog(carg, L_WARN,
			"process stdout read AFTER exit parse key=%s parser=%s pid=%d +%zd bytes total=%zu (had %zu at exit)\n",
			carg->key ? carg->key : "-",
			process_parser_name(carg),
			carg->child_req.pid,
			nread,
			carg->full_body ? carg->full_body->l : 0,
			carg->process_body_at_exit);
	}

	free_buffer(carg, buf);

	if (carg->period)
		uv_timer_set_repeat(carg->period_timer, carg->period);
}

static void _on_exit(uv_process_t *req, int64_t exit_status, int term_signal)
{
	context_arg *carg = req->data;
	const char *exit_reason = term_signal ? "signal" : "status";

	carglog(carg, L_INFO, "Process %p with pid %d with cmd %s exited with status %" PRId64 ", signal %d\n", req, req->pid, carg->host, exit_status, term_signal);
	carglog(carg, L_INFO,
		"process lifecycle exit key=%s parser=%s pid=%d reason=%s wait_ms=%" PRIu64 " timeouts=%" PRIu64 " status=%" PRId64 " signal=%d parsed=%d\n",
		carg->key ? carg->key : "-",
		process_parser_name(carg),
		req->pid,
		exit_reason,
		process_wait_ms(carg),
		carg->timeout_counter,
		exit_status,
		term_signal,
		carg->parsed);

	process_log_body_snapshot(carg, "exit parse before");
	carg->process_body_at_exit = carg->full_body ? carg->full_body->l : 0;
	if (!carg->parsed && carg->full_body && carg->full_body->l && carg->parser_handler)
		alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);
	else
		carglog(carg, L_INFO, "process exit parse skipped key=%s parser=%s (already parsed)\n",
			carg->key ? carg->key : "-", process_parser_name(carg));
	carg->process_exit_parsed = 1;
	process_log_body_snapshot(carg, "exit parse after");
	string_null(carg->full_body);

	carg->read_time_finish = setrtime();
	int64_t tsignal = term_signal;

	if (!carg->no_exit_status)
	{
		metric_add_labels3("alligator_process_exit_status", &exit_status, DATATYPE_INT, carg, "proto", "shell", "type", "aggregator", "key", carg->key);
		metric_add_labels3("alligator_process_term_signal", &tsignal, DATATYPE_INT, carg, "proto", "shell", "type", "aggregator", "key", carg->key);
	}

	if (!carg->no_metric)
	{
		metric_add_labels3("alligator_read_total", &carg->read_counter, DATATYPE_UINT, carg, "key", carg->key, "proto", "shell", "type", "aggregator");

		uint64_t read_time = getrtime_mcs(carg->read_time, carg->read_time_finish, 0);
		metric_add_labels3("alligator_read_duration_microseconds", &read_time, DATATYPE_UINT, carg, "proto", "shell", "type", "aggregator", "key", carg->key);
	}

	carg->lock = 0;

	carg->process_release_scheduled = 1;
	carg->process_released = 0;

	uv_read_stop((uv_stream_t *)&carg->channel);

	if (!uv_is_closing((uv_handle_t *)&carg->child_stdin))
		uv_close((uv_handle_t *)&carg->child_stdin, process_uv_handle_closed);
	else
		process_mark_released(carg, PROCESS_RELEASE_STDIN);

	if (!uv_is_closing((uv_handle_t *)&carg->channel))
		uv_close((uv_handle_t *)&carg->channel, process_uv_handle_closed);
	else
		process_mark_released(carg, PROCESS_RELEASE_CHANNEL);

	if (!uv_is_closing((uv_handle_t *)req))
		uv_close((uv_handle_t *)req, process_uv_handle_closed);
	else
		process_mark_released(carg, PROCESS_RELEASE_CHILD);

	carg_uv_detach_timers(carg);
	process_try_release(carg);
}

void timeout_exec_sentinel(uv_timer_t* timer) {
	context_arg *carg = timer->data;
	uv_timer_stop(timer);

	if (!carg)
	{
		return;
	}

	(carg->timeout_counter)++;
	process_log_body_snapshot(carg, "exec timeout before parse");
	carglog(carg, L_WARN, "process exec timeout key=%s parser=%s host=%s pid=%d timeout_ms=%" PRIu64 " parsed=%d\n",
		carg->key ? carg->key : "-",
		process_parser_name(carg),
		carg->host ? carg->host : "-",
		carg->child_req.pid,
		carg->timeout,
		carg->parsed);
	carglog(carg, L_WARN,
		"process lifecycle stop key=%s parser=%s pid=%d reason=timeout wait_ms=%" PRIu64 " timeout_ms=%" PRIu64 " reads=%" PRIu64 " body=%zu\n",
		carg->key ? carg->key : "-",
		process_parser_name(carg),
		carg->child_req.pid,
		process_wait_ms(carg),
		carg->timeout,
		carg->read_counter,
		carg->full_body ? carg->full_body->l : 0);

	if (!carg->parsed)
		alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);

	process_log_body_snapshot(carg, "exec timeout after parse");

	int err = uv_process_kill(&carg->child_req, SIGTERM);
	if (err)
		printf("Error kill process with pid %d: %s\n", carg->child_req.pid, uv_strerror(err));
}

void env_struct_process(void *funcarg, void* arg)
{
	env_struct *es = arg;
	string *stemplate = funcarg;

	string_cat(stemplate, "export ", 7);
	string_cat(stemplate, es->k, strlen(es->k));
	string_cat(stemplate, "='", 2);
	string_cat(stemplate, es->v, strlen(es->v));
	string_cat(stemplate, "'\n", 2);
}

void process_insert(void *arg)
{
	context_arg *carg = arg;
	alligator_ht_insert(ac->process_spawner, &(carg->node), carg, tommy_strhash_u32(0, carg->key));
	carg->process_spawner_registered = 1;
}

static int process_build_script(context_arg *carg)
{
	string *stemplate = string_init(1000);

	if (carg->env)
		alligator_ht_foreach_arg(carg->env, env_struct_process, stemplate);

	if (carg->work_dir)
	{
		string_cat(stemplate, "cd ", 3);
		string_cat(stemplate, carg->work_dir->s, carg->work_dir->l);
		string_cat(stemplate, "\n", 1);
	}

	if (carg->stdin_s && carg->stdin_l)
	{
		string_cat(stemplate, "echo '", 6);
		string_cat(stemplate, carg->stdin_s, carg->stdin_l);
		string_cat(stemplate, "' | '", 4);
	}

	string_cat(stemplate, carg->host, strlen(carg->host));
	if (carg->mesg)
	{
		string_cat(stemplate, " ", 1);
		string_cat(stemplate, carg->mesg, strlen(carg->mesg));
	}
	string_cat(stemplate, "\n", 1);

	if (carg->exec_script)
		free(carg->exec_script);

	carg->exec_script = stemplate->s;
	carg->exec_script_len = stemplate->l;
	stemplate->s = NULL;
	string_free(stemplate);

	return carg->exec_script ? 0 : -1;
}

static void process_stdin_write_cb(uv_write_t *req, int status)
{
	context_arg *carg = req->data;

	if (status < 0)
		carglog(carg, L_ERROR, "process stdin write for '%s': %s\n", carg->host, uv_strerror(status));
	else
		carglog(carg, L_DEBUG, "process stdin write completed for '%s', closing stdin pipe\n", carg->host);

	/* /bin/sh -s waits for EOF on stdin; close pipe after script write. */
	if (!uv_is_closing((uv_handle_t *)&carg->child_stdin))
	{
		carglog(carg, L_INFO,
			"process lifecycle stop key=%s parser=%s pid=%d reason=stdin_eof_sent wait_ms=%" PRIu64 "\n",
			carg->key ? carg->key : "-",
			process_parser_name(carg),
			carg->child_req.pid,
			process_wait_ms(carg));
		uv_close((uv_handle_t *)&carg->child_stdin, process_uv_handle_closed);
	}

	free(req);
}

static void process_feed_stdin(context_arg *carg)
{
	uv_write_t *write_req = malloc(sizeof(*write_req));
	if (!write_req)
	{
		carglog(carg, L_ERROR, "process stdin write alloc failed for '%s'\n", carg->host);
		uv_process_kill(&carg->child_req, SIGTERM);
		return;
	}

	write_req->data = carg;
	uv_buf_t buf = uv_buf_init(carg->exec_script, (unsigned int)carg->exec_script_len);
	int r = uv_write(write_req, (uv_stream_t*)&carg->child_stdin, &buf, 1, process_stdin_write_cb);
	if (r)
	{
		carglog(carg, L_ERROR, "process uv_write stdin for '%s': %s\n", carg->host, uv_strerror(r));
		free(write_req);
		uv_process_kill(&carg->child_req, SIGTERM);
	}
}

char* process_client(context_arg *carg)
{
	if (!carg)
	{
		carglog(carg, L_INFO, "exec is empty\n");
		return NULL;
	}

	if (process_build_script(carg) < 0)
	{
		carglog(carg, L_ERROR, "failed to build exec script for '%s'\n", carg->host);
		return NULL;
	}

	carglog(carg, L_INFO, "exec command via %s -s: '%s'\n", process_shell_path(), carg->host);
	carglog(carg, L_DEBUG, "exec script %zu bytes\n'%.*s'\n",
		carg->exec_script_len,
		(int)(carg->exec_script_len > 4096 ? 4096 : carg->exec_script_len),
		carg->exec_script);

	char **args = calloc(3, sizeof(char*));
	if (!args)
	{
		carglog(carg, L_ERROR, "failed to allocate exec argv for '%s'\n", carg->host);
		return NULL;
	}

	args[0] = NULL;
	args[1] = strdup("-s");
	args[2] = NULL;
	if (!args[1])
	{
		free(args);
		carglog(carg, L_ERROR, "failed to duplicate exec argv for '%s'\n", carg->host);
		return NULL;
	}

	carg->args = args;
	process_insert(carg);

	return "process";
}

void process_client_del(context_arg *carg)
{
	if (!carg)
		return;

	/* Never free a process context while libuv child/pipe handles are active. */
	if (carg->lock || carg->process_release_scheduled)
	{
		carg->remove_from_hash = 1;
		return;
	}

	// if not in callback
	if (carg->remove_from_hash)
		alligator_ht_remove_existing(ac->aggregators, &(carg->context_node));

	if (carg->process_spawner_registered)
		alligator_ht_remove_existing(ac->process_spawner, &(carg->node));
	carg_free(carg);
}

void on_process_spawn_repeat_period(uv_timer_t *timer);
void on_process_spawn(void* arg)
{
	context_arg *carg = arg;
	if (carg->lock)
		return;
	/* Previous child/pipe handles are still in close pipeline. */
	if (carg->process_release_scheduled)
		return;
	if (cluster_come_later(carg))
		return;

	carg->loop = get_threaded_loop_t_or_default(carg->threaded_loop_name);
	if (carg->period && !carg->read_counter) {
		carg->period_timer = alligator_cache_get(ac->uv_cache_timer, sizeof(uv_timer_t));
		carg->period_timer->data = carg;
		uv_timer_init(carg->loop, carg->period_timer);
		uv_timer_start(carg->period_timer, on_process_spawn_repeat_period, carg->period, 0);
	}

	carg->lock = 1;
	carg->parser_status = 0;
	carg->process_release_scheduled = 0;
	carg->process_released = 0;
	carg->process_exit_parsed = 0;
	carg->process_body_at_exit = 0;

	int r;
	uv_process_t *child_req = &carg->child_req;
	child_req->data = carg;
	carg->read_time = setrtime();

	uv_pipe_t *channel = &carg->channel;
	channel->data = carg;
	uv_pipe_init(carg->loop, channel, 1);

	uv_pipe_t *stdin_pipe = &carg->child_stdin;
	stdin_pipe->data = carg;
	uv_pipe_init(carg->loop, stdin_pipe, 0);

	uv_stdio_container_t *child_stdio = carg->child_stdio;
	child_stdio[ASTDIN_FILENO].flags = UV_CREATE_PIPE | UV_READABLE_PIPE;
	child_stdio[ASTDIN_FILENO].data.stream = (uv_stream_t*)stdin_pipe;
	child_stdio[ASTDOUT_FILENO].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
	child_stdio[ASTDOUT_FILENO].data.stream = (uv_stream_t*)channel;
	child_stdio[ASTDERR_FILENO].flags = UV_IGNORE;

	if (carg->args[0])
		free(carg->args[0]);
	carg->args[0] = strdup(process_shell_path());
	if (!carg->args[0])
	{
		carglog(carg, L_ERROR, "failed to duplicate exec shell for '%s'\n", carg->host);
		carg->process_release_scheduled = 1;
		carg->process_released = PROCESS_RELEASE_CHILD;
		process_close_or_released(carg, (uv_handle_t *)stdin_pipe, PROCESS_RELEASE_STDIN);
		process_close_or_released(carg, (uv_handle_t *)channel, PROCESS_RELEASE_CHANNEL);
		process_try_release(carg);
		carg->lock = 0;
		return;
	}

	uv_process_options_t *options = &carg->options;
	bzero(options, sizeof(uv_process_options_t));
	options->exit_cb = _on_exit;
	options->file = carg->args[0];
	options->args = carg->args;
	options->stdio = child_stdio;
	options->stdio_count = 3;

	r = uv_spawn(carg->loop, child_req, options);
	if (r) {
		carglog(carg, L_ERROR, "uv_spawn: %p error: %s\n", child_req, uv_strerror(r));
		process_spawn_failed(carg, stdin_pipe, channel);
	}
	else
	{
		carglog(carg, L_INFO, "process spawned key=%s parser=%s host=%s pid=%d timeout_ms=%" PRIu64 "\n",
			carg->key ? carg->key : "-",
			process_parser_name(carg),
			carg->host ? carg->host : "-",
			child_req->pid,
			carg->timeout);
		carglog(carg, L_INFO,
			"process lifecycle start key=%s parser=%s pid=%d state=wait timeout_ms=%" PRIu64 " period_ms=%" PRIu64 "\n",
			carg->key ? carg->key : "-",
			process_parser_name(carg),
			child_req->pid,
			carg->timeout,
			carg->period);

		process_feed_stdin(carg);
		uv_read_start((uv_stream_t*)channel, alloc_buffer, echo_read);

		carg->tt_timer = alligator_cache_get(ac->uv_cache_timer, sizeof(uv_timer_t));
		carg->tt_timer->data = carg;
		uv_timer_init(carg->loop, carg->tt_timer);
		uv_timer_start(carg->tt_timer, timeout_exec_sentinel, carg->timeout, 0);
	}
}

void for_on_process_spawn(void *arg)
{
	context_arg *carg = arg;
	if (carg->period && carg->read_counter)
		return;

	on_process_spawn(arg);
}

void on_process_spawn_repeat_period(uv_timer_t *timer)
{
	context_arg *carg = timer->data;
	if (!carg->period)
		return;

	on_process_spawn((void*)carg);
}

static void process_spawn_cb(uv_timer_t* handle) {
	alligator_ht_foreach(ac->process_spawner, for_on_process_spawn);
}

void process_handler()
{
	uv_loop_t *loop = ac->loop;

	uv_timer_init(loop, &ac->process_timer);
	uv_timer_start(&ac->process_timer, process_spawn_cb, 2500, ac->aggregator_repeat);
}
