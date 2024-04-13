#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <uv.h>
#include <uuid/uuid.h>
#include "dstructures/tommyds/tommyds/tommy.h"
#include "main.h"
#include "common/selector.h"
#include "events/uv_alloc.h"
#include "cluster/later.h"
#include "parsers/multiparser.h"
#include "common/mkdirp.h"
#include "common/logs.h"

void echo_read(uv_stream_t *server, ssize_t nread, const uv_buf_t* buf)
{
	context_arg *carg = server->data;
	carglog(carg, L_DEBUG, "Process %p with pid %d with cmd %s readed %zd bytes\n", &carg->child_req, carg->child_req.pid, carg->host, nread);

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
		metric_add_labels3("alligator_read", &carg->read_counter, DATATYPE_UINT, carg, "key", carg->key, "proto", "shell", "type", "aggregator");

		uint64_t read_time = getrtime_mcs(carg->read_time, carg->read_time_finish, 0);
		carg->read_time_counter += read_time;
		metric_add_labels3("alligator_read_time", &read_time, DATATYPE_UINT, carg, "proto", "shell", "type", "aggregtor", "key", carg->key);
		metric_add_labels3("alligator_read_total_time", &carg->read_time_counter, DATATYPE_UINT, carg, "proto", "shell", "type", "aggregtor", "key", carg->key);
	}

	carg->lock = 0;

	uv_close((uv_handle_t*)req, cmd_close);

	uv_read_stop((uv_stream_t*)&carg->channel);
	uv_close((uv_handle_t*)&carg->channel, NULL);

	uv_timer_stop(carg->tt_timer);
	alligator_cache_push(ac->uv_cache_timer, carg->tt_timer);
}

void timeout_exec_sentinel(uv_timer_t* timer) {
	context_arg *carg = timer->data;

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
}

char* process_client(context_arg *carg)
{
	if (!carg)
	{
		carglog(carg, L_INFO, "exec is empty\n");
		return NULL;
	}

	uuid_t uuid;
	uuid_generate_time_safe(uuid);
	char uuid_str[37];
	uuid_unparse_lower(uuid, uuid_str);

	size_t fsize = 255;
	char *fname = malloc(fsize);
	string *stemplate = string_init(1000);
	snprintf(fname, fsize-1, "%s/%s", ac->process_script_dir, uuid_str);
	carglog(carg, L_INFO, "exec command saved to: %s/%s: '%s'\n", ac->process_script_dir, uuid_str, carg->host);

	// TODO /bin/bash only linux, need customize
	string_cat(stemplate, "#!/bin/bash\n", 12);

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

	unlink(fname);

	mkdirp(ac->process_script_dir);
	write_to_file(fname, stemplate->s, stemplate->l, process_insert, carg);
	carglog(carg, L_DEBUG, "Saved command %zu\n'%s'\n", stemplate->l, stemplate->s);

	free(stemplate);

	char **args = calloc(2, sizeof(char*));
	args[0] = fname;
	args[1] = NULL;

	carg->args = args;
	//alligator_ht_insert(ac->process_spawner, &(carg->node), carg, tommy_strhash_u32(0, carg->key));

	return "process";
}

void process_client_del(context_arg *carg)
{
	if (!carg)
		return;

	// if not in callback
	if (carg->remove_from_hash)
		alligator_ht_remove_existing(ac->aggregators, &(carg->context_node));

	alligator_ht_remove_existing(ac->process_spawner, &(carg->node));
	unlink(carg->args[0]);
	carg_free(carg);
}

void on_process_spawn_repeat_period(uv_timer_t *timer);
void on_process_spawn(void* arg)
{
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;
	context_arg *carg = arg;
	if (carg->lock)
		return;
	if (cluster_come_later(carg))
		return;

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
	uv_pipe_init(loop, channel, 1);
	uv_stdio_container_t *child_stdio = carg->child_stdio;
	child_stdio[ASTDIN_FILENO].flags = UV_IGNORE;
	child_stdio[ASTDOUT_FILENO].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
	child_stdio[ASTDOUT_FILENO].data.stream = (uv_stream_t*)channel;
	child_stdio[ASTDERR_FILENO].flags = UV_IGNORE;

	uv_process_options_t *options = &carg->options;
	bzero(options, sizeof(uv_process_options_t));
	options->exit_cb = _on_exit;
	options->file = carg->args[0];
	options->args = carg->args;
	options->stdio = child_stdio;
	options->stdio_count = 3;

	r = uv_spawn(loop, child_req, options);
	if (r) {
		carglog(carg, L_ERROR, "uv_spawn: %p error: %s\n", child_req, uv_strerror(r));
		_on_exit(child_req, 0, 0);
	}
	else
	{
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
	extern aconf* ac;
	alligator_ht_foreach(ac->process_spawner, for_on_process_spawn);
}

void process_handler()
{
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;

	uv_timer_init(loop, &ac->process_timer);
	uv_timer_start(&ac->process_timer, process_spawn_cb, 2500, ac->aggregator_repeat);
}
