#include "main.h"
#include <unistd.h>
#include <string.h>
aconf *ac;

void ts_initialize()
{
	extern aconf *ac;
	ac->_namespace = calloc(1, sizeof(tommy_hashdyn));
	tommy_hashdyn_init(ac->_namespace);

	namespace_struct *ns = calloc(1, sizeof(*ns));
	ns->key = strdup("default");
	tommy_hashdyn_insert(ac->_namespace, &(ns->node), ns, tommy_strhash_u32(0, ns->key));
	ac->nsdefault = ns;

	ns->metric = calloc(1, sizeof(tommy_hashdyn));
	tommy_hashdyn_init(ns->metric);
}

void config_context_initialize()
{
	extern aconf *ac;
	ac->config_ctx = calloc(1, sizeof(tommy_hashdyn));
	tommy_hashdyn_init(ac->config_ctx);
	config_context *ctx;

	ctx = calloc(1, sizeof(*ctx));
	ctx->key = strdup("aggregate");
	ctx->handler = context_aggregate_parser;
	tommy_hashdyn_insert(ac->config_ctx, &(ctx->node), ctx, tommy_strhash_u32(0, ctx->key));

	ctx = calloc(1, sizeof(*ctx));
	ctx->key = strdup("entrypoint");
	ctx->handler = context_entrypoint_parser;
	tommy_hashdyn_insert(ac->config_ctx, &(ctx->node), ctx, tommy_strhash_u32(0, ctx->key));
}

aconf* configuration()
{
	ac = calloc(1, sizeof(*ac));
	ac->aggregator = calloc(1, sizeof(tommy_hashdyn));
	ac->uggregator = calloc(1, sizeof(tommy_hashdyn));

	ac->process_spawner = calloc(1, sizeof(tommy_hashdyn));
	ac->process_script_dir = "/var/alligator/spawner";
	ac->process_cnt = 0;
	tommy_hashdyn_init(ac->aggregator);
	tommy_hashdyn_init(ac->process_spawner);

	ac->request_cnt = 0;
	ts_initialize();
	config_context_initialize();

	return ac;
}

int main(int argc, char **argv)
{
	puts("alligator started");
	ac = configuration();
	uv_loop_t *loop = ac->loop = uv_default_loop();
	if (argc < 2)
		split_config("alligator.conf");
	else
		split_config(argv[1]);


	tcp_client_handler();
	//process_handler();
	get_system_metrics();

	return uv_run(loop, UV_RUN_DEFAULT);
}
