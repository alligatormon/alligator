#include "parsers/mysql.h"
#include "query/query.h"
#include "main.h"

my_library* mysql_module_init()
{
	module_t *libmy = tommy_hashdyn_search(ac->modules, module_compare, "mysql", tommy_strhash_u32(0, "mysql"));
	if (!libmy)
	{
		printf("No defined libmysql library in configuration\n");
		return NULL;
	}


	my_library *mylib = calloc(1, sizeof(*mylib));

	mylib->mysql_init = (void*)module_load(libmy->path, "mysql_init", &mylib->mysql_init_lib);
	if (!mylib->mysql_init)
	{
		printf("Cannot get mysql_init from libmy\n");
		//mysql_module_free(mylib);
		return NULL;
	}

	mylib->mysql_real_connect = (void*)module_load(libmy->path, "mysql_real_connect", &mylib->mysql_real_connect_lib);
	if (!mylib->mysql_real_connect)
	{
		printf("Cannot get mysql_real_connect from libmy\n");
		//mysql_module_free(mylib);
		return NULL;
	}

	mylib->mysql_query = (void*)module_load(libmy->path, "mysql_query", &mylib->mysql_query_lib);
	if (!mylib->mysql_query)
	{
		printf("Cannot get mysql_query from libmy\n");
		//mysql_module_free(mylib);
		return NULL;
	}

	mylib->mysql_error = (void*)module_load(libmy->path, "mysql_error", &mylib->mysql_error_lib);
	if (!mylib->mysql_error)
	{
		printf("Cannot get mysql_error from libmy\n");
		//mysql_module_free(mylib);
		return NULL;
	}

	mylib->mysql_close = module_load(libmy->path, "mysql_close", &mylib->mysql_close_lib);
	if (!mylib->mysql_close)
	{
		printf("Cannot get mysql_close from libmy\n");
		//mysql_module_free(mylib);
		return NULL;
	}

	mylib->mysql_store_result = (void*)module_load(libmy->path, "mysql_store_result", &mylib->mysql_store_result_lib);
	if (!mylib->mysql_store_result)
	{
		printf("Cannot get mysql_store_result from libmy\n");
		//mysql_module_free(mylib);
		return NULL;
	}

	mylib->mysql_num_fields = (void*)module_load(libmy->path, "mysql_num_fields", &mylib->mysql_num_fields_lib);
	if (!mylib->mysql_num_fields)
	{
		printf("Cannot get mysql_num_fields from libmy\n");
		//mysql_module_free(mylib);
		return NULL;
	}

	mylib->mysql_fetch_row = (void*)module_load(libmy->path, "mysql_fetch_row", &mylib->mysql_fetch_row_lib);
	if (!mylib->mysql_fetch_row)
	{
		printf("Cannot get mysql_fetch_row from libmy\n");
		//mysql_module_free(mylib);
		return NULL;
	}

	mylib->mysql_fetch_field_direct = (void*)module_load(libmy->path, "mysql_fetch_field_direct", &mylib->mysql_fetch_field_direct_lib);
	if (!mylib->mysql_fetch_field_direct)
	{
		printf("Cannot get mysql_fetch_field_direct from libmy\n");
		//mysql_module_free(mylib);
		return NULL;
	}

	mylib->mysql_free_result = module_load(libmy->path, "mysql_free_result", &mylib->mysql_free_result_lib);
	if (!mylib->mysql_free_result)
	{
		printf("Cannot get mysql_free_result from libmy\n");
		//mysql_module_free(mylib);
		return NULL;
	}

	return mylib;
}

int mysql_load_module()
{
	if (!ac->mylib)
	{
		my_library* mylib = mysql_module_init();
		if (mylib)
			ac->mylib = mylib;
		else
		{
			puts("Cannot initialize mysql library");
			return 0;
		}
	}
	return 1;
}

void mysql_print_result(MYSQL *con)
{
	MYSQL_RES *result = ac->mylib->mysql_store_result(con);
	if (!result)
	{
		fprintf(stderr, "%s\n", ac->mylib->mysql_error(con));
		return;
	}

	unsigned int num_fields = ac->mylib->mysql_num_fields(result);

	MYSQL_ROW row;
	while ((row = ac->mylib->mysql_fetch_row(result)))
	{
		for (unsigned int i = 0; i < num_fields; i++)
		{
			MYSQL_FIELD *field = ac->mylib->mysql_fetch_field_direct(result, i);
			printf("%s:%s ", field->name, row[i] ? row[i] : "NULL");
		}
		puts("");
	}
	ac->mylib->mysql_free_result(result);
}

void mysql_queries_foreach(void *funcarg, void* arg)
{
	context_arg *carg = (context_arg*)funcarg;
	MYSQL *con = carg->data;
	query_node *qn = arg;
	if (carg->log_level > 1)
	{
		puts("+-+-+-+-+-+-+-+");
		printf("run datasource '%s', make '%s': '%s'\n", qn->datasource, qn->make, qn->expr);
	}

	if (ac->mylib->mysql_query(con, qn->expr)) 
	{
		fprintf(stderr, "%s\n", ac->mylib->mysql_error(con));
		ac->mylib->mysql_close(con);
		return;
	}

	mysql_print_result(con);
}

void mysql_run(void* arg)
{
	puts("mysql_run");
	if (!mysql_load_module())
		return;

	MYSQL *con = ac->mylib->mysql_init(NULL);
	
	if (con == NULL) 
	{
		fprintf(stderr, "%s\n", ac->mylib->mysql_error(con));
		return;
	}
	
	context_arg *carg = arg;
	printf("url: %s\n", carg->url);
	//if (ac->mylib->mysql_real_connect(con, "localhost", "root", "root_pswd", NULL, 0, NULL, 0) == NULL) 
	if (ac->mylib->mysql_real_connect(con, carg->host, carg->user, carg->password, NULL, strtoull(carg->port, NULL, 10), NULL, 0) == NULL) 
	{
		fprintf(stderr, "%s\n", ac->mylib->mysql_error(con));
		ac->mylib->mysql_close(con);
		return;
	}  
	
	if (ac->mylib->mysql_query(con, "SELECT table_schema \"DB Name\", ROUND(SUM(data_length + index_length) / 1024 / 1024, 1) \"DB Size in MB\" FROM information_schema.tables GROUP BY table_schema;")) 
	{
		fprintf(stderr, "%s\n", ac->mylib->mysql_error(con));
		ac->mylib->mysql_close(con);
		return;
	}

	mysql_print_result(con);

	if (carg->name)
	{
		query_ds *qds = query_get(carg->name);
		if (carg->log_level > 1)
			printf("found queries for datasource: %s: %p\n", carg->name, qds);
		if (qds)
		{
			carg->data = con;
			tommy_hashdyn_foreach_arg(qds->hash, mysql_queries_foreach, carg);
		}
	}

	ac->mylib->mysql_close(con);
}

void mysql_timer(void *arg) {
	extern aconf* ac;
	usleep(ac->aggregator_startup * 1000);
	while ( 1 )
	{
		puts("tommy_hashdyn_foreach");
		tommy_hashdyn_foreach(ac->my_aggregator, mysql_run);
		usleep(ac->aggregator_repeat * 1000);
	}
}

void mysql_timer_without_thread(uv_timer_t* handle) {
	(void)handle;
	tommy_hashdyn_foreach(ac->my_aggregator, mysql_run);
}

void mysql_without_thread()
{
	uv_timer_t *timer1 = calloc(1, sizeof(*timer1));
	uv_timer_init(ac->loop, timer1);
	uv_timer_start(timer1, mysql_timer_without_thread, ac->aggregator_startup, ac->aggregator_repeat);
}

void mysql_client_handler()
{
	// uncomment for thread
	//uv_thread_t th;
	//uv_thread_create(&th, mysql_timer, NULL);
	mysql_without_thread();
}

void mysql_client(context_arg* carg)
{
	puts("mysql");
	if (!carg)
		return;

	carg->key = malloc(255);
	snprintf(carg->key, 255, "%s", carg->host);

	tommy_hashdyn_insert(ac->my_aggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));
}

void mysql_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("mysql");
	actx->handlers = 1;
	actx->handler = malloc(sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = NULL;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = NULL;
	strlcpy(actx->handler[0].key, "mysql", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
