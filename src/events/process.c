#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <uv.h>
#include "dstructures/tommyds/tommyds/tommy.h"
#include "main.h"
#include "events/uv_alloc.h"
//#define CDM "curl -I https://ya.ru"
#define CDM1 "curl -I https://ya.ru"
#define CDM2 "curl -I https://r0.ru"
#define CDM3 "curl -I https://mail.ru"

typedef struct process_info
{
	char *key;
	uv_process_options_t *options;
	uv_pipe_t *channel;
	uv_stdio_container_t *child_stdio;
	void *parser_handler;
	char** args;

	tommy_node node;
} process_info;

void echo_read(uv_stream_t *server, ssize_t nread, const uv_buf_t* buf)
{
	if (nread == -1) {
		fprintf(stderr, "error echo_read");
		return;
	}
	process_info *pinfo = server->data;

	alligator_multiparser(buf->base, buf->len, pinfo->parser_handler, NULL);

	if (buf->base)
	{
		//printf("I:'%s'\n", buf->base);
		free(buf->base);
	}
	// call abort
	//free(server);
}

static void _on_exit(uv_process_t *req, int64_t exit_status, int term_signal)
{
	//fprintf(stdout, "Process %d exited with status %" PRId64 ", signal %d\n", req->pid, exit_status, term_signal);
	uv_close((uv_handle_t*)req, NULL);
	free(req);
}

//void timeout_process(uv_work_t* req)
//{
//	printf("Sleeping...\n");
//	sleep(1);
//	uv_timer_stop(req->data);
//	//printf("UnSleeping...\n");
//}

//void timer_exec_sentinel(uv_timer_t* timer) {
//	uint64_t timestamp = uv_hrtime();
//	//uv_work_t* work_req = (uv_work_t*)malloc(sizeof(*work_req));
//	//uv_queue_work(uv_default_loop(), work_req, timeout_process, on_after_work);
//}

void put_to_loop_cmd(char *cmd, void *parser_handler)
{
	if (!cmd)
	{
		puts("exec is empty");
		return;
	}
	extern aconf* ac;
	size_t len = strlen(cmd);

	char *fname = malloc(255);
	char *template = malloc(1000);
	snprintf(fname, 255, "%s/%"d64"", ac->process_script_dir, ac->process_cnt);
	printf("exec command saved to: %s/%"d64"\n", ac->process_script_dir, ac->process_cnt);
	// TODO /bin/bash only linux, need customize
	int rc = snprintf(template, 1000, "#!/bin/bash\n%s\n", cmd);
	free(cmd);
	unlink(fname);
	write_to_file(fname, template, rc);
	(ac->process_cnt)++;

	int64_t i, y, start, n = 1;
	char *tmp = fname;
	for (i=0; i<len; i++)
	{
		if ( isspace(tmp[i]) )
		{
			for (; isspace(tmp[i]); i++);
			n++;
		}
	}
	char** args = calloc(n+1, sizeof(char*));

	for (i=0, y=0, start=0; y<n; i++)
	{
		if ( isspace(tmp[i]) )
		{
			int64_t end = i;
			for (; isspace(tmp[i]); i++);
			args[y] = strndup(tmp+start, end-start);

			start = i;
			y++;
		}
	}
	args[n] = NULL;




	//uv_pipe_t *channel = calloc(1, sizeof(uv_pipe_t));
	//uv_pipe_init(loop, channel, 1);
	//uv_stdio_container_t *child_stdio = calloc(3, sizeof(uv_stdio_container_t)); // {stdin, stdout, stderr}
	//child_stdio[STDIN_FILENO].flags = UV_IGNORE;
	//child_stdio[STDOUT_FILENO].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
	//child_stdio[STDOUT_FILENO].data.stream = (uv_stream_t*)channel;
	//child_stdio[STDERR_FILENO].flags = UV_IGNORE;

	//uv_process_options_t *options = calloc(1, sizeof(uv_process_options_t));
	//options->exit_cb = _on_exit;
	//options->file = args[0];
	//options->args = args;
	//options->stdio = child_stdio;
	//options->stdio_count = 3;
	//printf (":%d:\n", options->stdio_count);

	process_info *pinfo = calloc(1, sizeof(*pinfo));
	//pinfo->options = options;
	pinfo->key = cmd;
	//pinfo->channel = channel;
	//pinfo->child_stdio = child_stdio;
	pinfo->parser_handler = parser_handler;
	pinfo->args = args;
	tommy_hashdyn_insert(ac->process_spawner, &(pinfo->node), pinfo, tommy_strhash_u32(0, pinfo->key));
}

void on_process_spawn(void* arg)
{
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;
	process_info *pinfo = arg;

	int r;
	uv_process_t *child_req = malloc(sizeof(uv_process_t));

	uv_pipe_t *channel = malloc(sizeof(uv_pipe_t));
	channel->data = pinfo;
	uv_pipe_init(loop, channel, 1);
	uv_stdio_container_t *child_stdio = malloc(3*sizeof(uv_stdio_container_t));
	child_stdio[STDIN_FILENO].flags = UV_IGNORE;
	child_stdio[STDOUT_FILENO].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
	child_stdio[STDOUT_FILENO].data.stream = (uv_stream_t*)channel;
	child_stdio[STDERR_FILENO].flags = UV_IGNORE;

	uv_process_options_t *options = calloc(1, sizeof(uv_process_options_t));
	options->exit_cb = _on_exit;
	options->file = pinfo->args[0];
	options->args = pinfo->args;
	options->stdio = child_stdio;
	options->stdio_count = 3;

	r = uv_spawn(loop, child_req, options);
	if (r) {
		fprintf(stderr, "uv_spawn: %s\n", uv_strerror(r));
	}
	else
	{
		uv_read_start((uv_stream_t*)channel, alloc_buffer, echo_read);
		//uv_timer_t *timer = malloc(sizeof(uv_timer_t));
		//uv_timer_init(loop, timer);
		//uv_timer_start(timer, timer_exec_sentinel, 0, 10);
	}
}

static void process_spawn_cb(uv_timer_t* handle) {
	extern aconf* ac;
	tommy_hashdyn_foreach(ac->process_spawner, on_process_spawn);
}

void process_handler()
{
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;

	uv_timer_t *timer = calloc(1, sizeof(*timer));
	uv_timer_init(loop, timer);
	uv_timer_start(timer, process_spawn_cb, 2500, 10000);
}

//int main()
//{
//	uv_loop_t* loop = uv_default_loop();
//	const int N = 3;
//	uv_pipe_t *channel = calloc(N, sizeof(uv_pipe_t));
//	uv_process_options_t **options = calloc(N, sizeof(uv_process_options_t*));
//	uv_timer_t *timer = calloc(N, sizeof(uv_timer_t));
//	put_to_loop_cmd(CDM1, strlen(CDM1), &channel[0]);
//	put_to_loop_cmd(CDM2, strlen(CDM2), &channel[1]);
//	put_to_loop_cmd(CDM3, strlen(CDM3), &channel[2]);
//
//
//	return uv_run(loop, UV_RUN_DEFAULT);
//}
