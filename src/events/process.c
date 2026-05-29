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

void echo_read(uv_stream_t *server, ssize_t nread, const uv_buf_t* buf)
{
	context_arg *carg = server->data;
	carglog(carg, L_DEBUG, "Process %p with pid %d with cmd %s read %zd bytes\n", &carg->child_req, carg->child_req.pid, carg->host, nread);

	if (nread == -1)
	{
		carglog(carg, L_ERROR, "error echo_read");
		free_buffer(carg, buf);
		return;
	}

	if (nread < 0)
	{
		free_buffer(carg, buf);
		return;
	}

	(carg->read_counter)++;

	string_cat(carg->full_body, buf->base, nread);

	free_buffer(carg, buf);

	if (carg->period)
		uv_timer_set_repeat(carg->period_timer, carg->period);
}

void cmd_close(uv_handle_t *handle)
{
	context_arg *carg = handle->data;
	carglog(carg, L_INFO, "run cmd close %p with cmd %s\n", handle, carg->host);

	if (carg->context_ttl)
	{
		r_time time = setrtime();
		if (time.sec >= carg->context_ttl)
		{
			carg->remove_from_hash = 1;
			smart_aggregator_del(carg);
		}
	}
}

static void _on_exit(uv_process_t *req, int64_t exit_status, int term_signal)
{
	context_arg *carg = req->data;

	carglog(carg, L_INFO, "Process %p with pid %d with cmd %s exited with status %" PRId64 ", signal %d\n", req, req->pid, carg->host, exit_status, term_signal);

	alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);
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

	uv_close((uv_handle_t*)req, cmd_close);

	uv_read_stop((uv_stream_t*)&carg->channel);
	uv_close((uv_handle_t*)&carg->channel, NULL);

	if (carg->tt_timer)
	{
		uv_timer_stop(carg->tt_timer);
		alligator_cache_push(ac->uv_cache_timer, carg->tt_timer);
	}
}

void timeout_exec_sentinel(uv_timer_t* timer) {
	context_arg *carg = timer->data;
	uv_timer_stop(timer);

	if (!carg)
	{
		return;
	}

	carglog(carg, L_INFO, "%"u64": timeout tcp client %p(%p:%p) with key %s, hostname %s, port: %s tls: %d, timeout: %"u64"\n", carg->count++, carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, carg->timeout);
	(carg->timeout_counter)++;

	if (!carg->parsed)
		alligator_multiparser(carg->full_body->s, carg->full_body->l, carg->parser_handler, NULL, carg);

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

	free(req);
	uv_close((uv_handle_t*)&carg->child_stdin, NULL);
}

static void process_feed_stdin(context_arg *carg)
{
	uv_write_t *write_req = malloc(sizeof(*write_req));
	if (!write_req)
	{
		carglog(carg, L_ERROR, "process stdin write alloc failed for '%s'\n", carg->host);
		uv_close((uv_handle_t*)&carg->child_stdin, NULL);
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
		uv_close((uv_handle_t*)&carg->child_stdin, NULL);
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
		uv_close((uv_handle_t*)stdin_pipe, NULL);
		uv_close((uv_handle_t*)channel, NULL);
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
		uv_close((uv_handle_t*)stdin_pipe, NULL);
		uv_close((uv_handle_t*)channel, NULL);
		_on_exit(child_req, 0, 0);
	}
	else
	{
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
