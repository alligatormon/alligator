#include "parsers/mysql.h"
#include "query/query.h"
#include "main.h"
#define MY_TYPE_MYSQL 0
#define MY_TYPE_SPHINXSEARCH 1

typedef struct my_data {
	MYSQL *con;
	int type;
} my_data;

void mysql_run(void* arg);

my_library* mysql_module_init()
{
	module_t *libmy = alligator_ht_search(ac->modules, module_compare, "mysql", tommy_strhash_u32(0, "mysql"));
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

void mysql_print_result(context_arg *carg, MYSQL *con, query_node *qn)
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

void mysql_execution(context_arg *carg, MYSQL *con, query_node *qn)
{
	MYSQL_RES *result = ac->mylib->mysql_store_result(con);
	if (!result)
	{
		if (carg->log_level > 1)
			fprintf(stderr, "%s\n", ac->mylib->mysql_error(con));

		char reason[255];
		uint64_t val = 1;
		uint64_t reason_size = strlcpy(reason, ac->mylib->mysql_error(con), 255);
		prometheus_metric_name_normalizer(reason, reason_size);
		metric_add_labels2("mysql_error", &val, DATATYPE_UINT, carg, "name",  carg->name, "reason", reason);
        carg->parser_status = 0;

		return;
	}

	unsigned int num_fields = ac->mylib->mysql_num_fields(result);

	MYSQL_ROW row;
	while ((row = ac->mylib->mysql_fetch_row(result)))
	{
		alligator_ht *hash = alligator_ht_init(NULL);

		if (carg->ns)
			labels_hash_insert_nocache(hash, "dbname", carg->ns);

		for (unsigned int i = 0; i < num_fields; i++)
		{
			MYSQL_FIELD *field = ac->mylib->mysql_fetch_field_direct(result, i);
			char *colname = field->name;
			char *res = row[i];
			if (!res)
				continue;

			if (field->type == 244) // BOOL
			{
				query_field *qf = query_field_get(qn->qf_hash, colname);
				if (qf)
				{
					if (carg->log_level > 2)
						printf("\tbool value '%s'\n", res);
					if (!strncmp(res, "t", 0))
						qf->i = 1;
					else
						qf->i = 0;

					qf->type = DATATYPE_INT;
				}
				else
				{
					if (carg->log_level > 2)
						printf("\tfield '%s': '%s'\n", colname, res);
					labels_hash_insert_nocache(hash, colname, res);
				}
			}
			else if (field->type == MYSQL_TYPE_FLOAT || field->type == MYSQL_TYPE_DOUBLE)
			{
				query_field *qf = query_field_get(qn->qf_hash, colname);
				if (qf)
				{
					if (carg->log_level > 2)
						printf("\tfloat value '%s'\n", res);
					qf->d = strtod(res, NULL);

					qf->type = DATATYPE_DOUBLE;
				}
				else
				{
					if (carg->log_level > 2)
						printf("\tfield '%s': '%s'\n", colname, res);
					labels_hash_insert_nocache(hash, colname, res);
				}
			}
			//else if (field->type == MYSQL_TYPE_LONG || field->type == MYSQL_TYPE_SHORT || field->type == MYSQL_TYPE_DECIMAL || field->type == MYSQL_TYPE_TINY || field->type == MYSQL_TYPE_INT24 || field->type == MYSQL_TYPE_LONGLONG)
			else
			{
				query_field *qf = query_field_get(qn->qf_hash, colname);
				if (qf)
				{
					if (carg->log_level > 2)
						printf("\tvalue '%s'\n", res);
					qf->i = strtoll(res, NULL, 10);
					qf->type = DATATYPE_INT;
				}
				else
				{
					if (carg->log_level > 2)
						printf("\tfield '%s': '%s'\n", colname, res);
					labels_hash_insert_nocache(hash, colname, res);
				}

			}
			//if (field->type == MYSQL_TYPE_VARCHAR || field->type == MYSQL_TYPE_STRING || field->type == MYSQL_TYPE_VAR_STRING)
		}

		qn->labels = hash;
		qn->carg = carg;
		query_set_values(qn);
		labels_hash_free(hash);
	}
	ac->mylib->mysql_free_result(result);
}

void sphinxsearch_callback(context_arg *carg, MYSQL *con, query_node *qn)
{
	MYSQL_RES *result = ac->mylib->mysql_store_result(con);
	if (!result)
	{
		if (carg->log_level > 1)
			fprintf(stderr, "%s\n", ac->mylib->mysql_error(con));

		char reason[255];
		uint64_t reason_size = strlcpy(reason, ac->mylib->mysql_error(con), 255);
		uint64_t val = 1;
		prometheus_metric_name_normalizer(reason, reason_size);
		metric_add_labels2("mysql_error", &val, DATATYPE_UINT, carg, "name",  carg->name, "reason", reason);

		return;
	}

	unsigned int num_fields = ac->mylib->mysql_num_fields(result);

	char *qtype = (char*)qn;
	char mname[255];
	MYSQL_ROW row;
	while ((row = ac->mylib->mysql_fetch_row(result)))
	{
		if (!strcmp(qtype, "status") || !strcmp(qtype, "variables") || !strcmp(qtype, "plan"))
		{
			char *Counter = row[0];
			char *Value = row[1];
			snprintf(mname, 254, "sphinxsearch_%s_%s", qtype, Counter);
			if (isdigit(*Value))
			{
				if (strstr(Value, "."))
				{
					double val = strtod(Value, NULL);
					metric_add_auto(mname, &val, DATATYPE_DOUBLE, carg);
				}
				else
				{
					int64_t val = strtoll(Value, NULL, 10);
					metric_add_auto(mname, &val, DATATYPE_INT, carg);
				}
			}
		}
		else
		{
			char status[255];
			char ConnID[255];
			char Host[255];
			char Tid[255];
			char Name[255];
			char Proto[255];
			char State[255];
			char Info[255];
			char Inidle[255];
			*status = 0;
			*ConnID = 0;
			*Host = 0;
			*Tid = 0;
			*Name = 0;
			*Proto = 0;
			*State = 0;
			*Info = 0;
			*Inidle = 0;
			for (unsigned int i = 0; i < num_fields; i++)
			{
				MYSQL_FIELD *field = ac->mylib->mysql_fetch_field_direct(result, i);
				char *colname = field->name;
				char *res = row[i];
				if (!res)
					continue;

				if (!strcmp(colname, "Status"))
					strlcpy(status, res, strlen(res)+1);
				else if (!strcmp(colname, "ConnID"))
					strlcpy(ConnID, res, strlen(res)+1);
				else if (!strcmp(colname, "Host"))
					strlcpy(Host, res, strlen(res)+1);
				else if (!strcmp(colname, "Tid"))
					strlcpy(Tid, res, strlen(res)+1);
				else if (!strcmp(colname, "Name"))
					strlcpy(Name, res, strlen(res)+1);
				else if (!strcmp(colname, "Proto"))
					strlcpy(Proto, res, strlen(res)+1);
				else if (!strcmp(colname, "State"))
					strlcpy(State, res, strlen(res)+1);
				else if (!strcmp(colname, "Info"))
					strlcpy(Info, res, strlen(res)+1);
				else if (!strcmp(colname, "In idle"))
					strlcpy(Inidle, res, strlen(res)+1);
				else if (!strncmp(colname, "Work time", 9)) {}
				else
				{
					if (isdigit(*res))
					{
						alligator_ht *hash = alligator_ht_init(NULL);
						if (*status)
							labels_hash_insert_nocache(hash, "status", status);
						if (*ConnID)
							labels_hash_insert_nocache(hash, "ConnID", ConnID);
						if (*Host)
							labels_hash_insert_nocache(hash, "Host", Host);
						if (*Tid)
							labels_hash_insert_nocache(hash, "Tid", Tid);
						if (*Name)
							labels_hash_insert_nocache(hash, "Name", Name);
						if (*Proto)
							labels_hash_insert_nocache(hash, "Proto", Proto);
						if (*State)
							labels_hash_insert_nocache(hash, "State", State);
						if (*Info)
							labels_hash_insert_nocache(hash, "Info", Info);
						if (*Inidle)
							labels_hash_insert_nocache(hash, "InIdle", Inidle);

						snprintf(mname, 254, "sphinxsearch_%s_%s", qtype, colname);
						if (carg->log_level > 1)
							printf("'%s' (%s): '%s'\n", colname, mname, res);
						metric_name_normalizer(mname, strlen(mname));
						if (strstr(res, "."))
						{
							double val = strtod(res, NULL);
							metric_add(mname, hash, &val, DATATYPE_DOUBLE, carg);
						}
						else if (strstr(res, "ms"))
						{
							double val = strtod(res, NULL) / 1000;
							metric_add(mname, hash, &val, DATATYPE_DOUBLE, carg);
						}
						else if (strstr(res, "us"))
						{
							double val = strtod(res, NULL) / 1000000;
							metric_add(mname, hash, &val, DATATYPE_DOUBLE, carg);
						}
						else
						{
							int64_t val = strtoll(res, NULL, 10);
							metric_add(mname, hash, &val, DATATYPE_INT, carg);
						}
					}
					else
					{
						snprintf(mname, 254, "sphinxsearch_%s_%s", qtype, colname);
						printf("'%s' (%s): '%s'\n", colname, mname, res);
					}
				}
			}
		}
	}
	ac->mylib->mysql_free_result(result);
}

void mysql_alligator_query(context_arg *carg, MYSQL *con, char *query, query_node *qn, void callback(context_arg*, MYSQL*, query_node *qn))
{
	if (ac->mylib->mysql_query(con, query)) 
	{
		fprintf(stderr, "%s\n", ac->mylib->mysql_error(con));
		ac->mylib->mysql_close(con);
		return;
	}

	callback(carg, con, qn);
}

void mysql_queries_foreach(void *funcarg, void* arg)
{
	context_arg *carg = (context_arg*)funcarg;
	my_data *data = carg->data;
	MYSQL *con = data->con;
	query_node *qn = arg;
	if (carg->log_level > 1)
	{
		puts("+-+-+-+-+-+-+-+");
		printf("run datasource '%s', make '%s': '%s'\n", qn->datasource, qn->make, qn->expr);
	}

	mysql_alligator_query(carg, con, qn->expr, qn, mysql_execution);
}

void mysql_get_databases(MYSQL *con, context_arg *carg)
{
	if (ac->mylib->mysql_query(con, "SHOW DATABASES"))
	{
		fprintf(stderr, "%s\n", ac->mylib->mysql_error(con));
		ac->mylib->mysql_close(con);
		return;
	}

	context_arg *db_carg = malloc(sizeof(*db_carg));
	size_t url_len = strlen(carg->url) + 1024;
	char *url = malloc(url_len);
	size_t name_len = strlen(carg->name) + 1024;
	char *name = malloc(name_len);
	char *ns = malloc(1024);

	memcpy(db_carg, carg, sizeof(*db_carg));
	db_carg->url = url;
	db_carg->name = name;
	db_carg->ns = ns;
	db_carg->data_lock = 1;

	int wildcard = 0;
	snprintf(name, name_len - 1, "%s/*", carg->name);
	if (query_get(name))
		wildcard = 1;



	MYSQL_RES *result = ac->mylib->mysql_store_result(con);
	if (!result)
	{
		if (carg->log_level > 1)
			fprintf(stderr, "command get databases failed: %s\n", ac->mylib->mysql_error(con));

		char reason[255];
		uint64_t reason_size = strlcpy(reason, ac->mylib->mysql_error(con), 255);
		uint64_t val = 1;
		prometheus_metric_name_normalizer(reason, reason_size);
		metric_add_labels2("mysql_error", &val, DATATYPE_UINT, carg, "name",  carg->name, "reason", reason);

		return;
	}

	unsigned int num_fields = ac->mylib->mysql_num_fields(result);

	MYSQL_ROW row;
	while ((row = ac->mylib->mysql_fetch_row(result)))
	{
		for (unsigned int i = 0; i < num_fields; i++)
		{
			//MYSQL_FIELD *field = ac->mylib->mysql_fetch_field_direct(result, i);

			char *resp = row[i];
			snprintf(db_carg->url, url_len - 1, "%s/%s", carg->url, resp);
			strlcpy(db_carg->ns, resp, 1024);

			snprintf(db_carg->name, name_len - 1, "%s/*", carg->name);
			if (wildcard)
			{
				if (carg->log_level > 1)
					printf("run wildcard queries\n");
				mysql_run(db_carg);
			}

			snprintf(db_carg->name, name_len - 1, "%s/%s", carg->name, resp);
			if (query_get(db_carg->name))
			{
				if (query_get(db_carg->name))
				{
					if (carg->log_level > 1)
						printf("exec queries database '%s'\n", resp);

					mysql_run(db_carg);
				}
			}
		}
	}

	ac->mylib->mysql_free_result(result);

	free(db_carg);
	free(ns);
	free(url);
	free(name);
}

void sphinxsearch_queries(context_arg *carg)
{
	my_data *data = carg->data;
	MYSQL *con = data->con;
	mysql_alligator_query(carg, con, "SHOW VARIABLES", (query_node *)"variables", sphinxsearch_callback);
	mysql_alligator_query(carg, con, "SHOW STATUS", (query_node *)"status", sphinxsearch_callback);
	mysql_alligator_query(carg, con, "SHOW PLAN", (query_node *)"plan", sphinxsearch_callback);
	mysql_alligator_query(carg, con, "SHOW PROFILE", (query_node *)"profile", sphinxsearch_callback);
	mysql_alligator_query(carg, con, "SHOW THREADS", (query_node *)"threads", sphinxsearch_callback);
}

void mysql_run(void* arg)
{
	uint64_t unval = 0;
	uint64_t val = 1;
	context_arg *carg = arg;

	if (!mysql_load_module())
	{
		char reason[255];
		strlcpy(reason, "error with load mysql module", 255);
		metric_add_labels2("mysql_error", &val, DATATYPE_UINT, carg, "name",  carg->name, "reason", reason);
		metric_add_labels6("alligator_connect_ok", &unval, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", "mysql", "name", carg->name);
	    metric_add_labels6("alligator_parser_status", &unval, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", "mysql", "name", carg->name);

		return;
	}

	MYSQL *con = ac->mylib->mysql_init(NULL);
	if (con == NULL) 
	{
		if (carg->log_level > 0)
			fprintf(stderr, "%s\n", ac->mylib->mysql_error(con));

		char reason[255];
		uint64_t reason_size = strlcpy(reason, ac->mylib->mysql_error(con), 255);
		prometheus_metric_name_normalizer(reason, reason_size);
		metric_add_labels2("mysql_error", &val, DATATYPE_UINT, carg, "name",  carg->name, "reason", reason);
		metric_add_labels6("alligator_connect_ok", &unval, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", "mysql", "name", carg->name);
	    metric_add_labels6("alligator_parser_status", &unval, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", "mysql", "name", carg->name);

		return;
	}
	
	my_data *data = carg->data;
	data->con = con;
	if (ac->mylib->mysql_real_connect(con, carg->host, carg->user, carg->password, carg->ns ? carg->ns : NULL, strtoull(carg->port, NULL, 10), NULL, 0) == NULL) 
	{
		if (carg->log_level > 0)
			fprintf(stderr, "%s\n", ac->mylib->mysql_error(con));

		char reason[255];
		uint64_t reason_size = strlcpy(reason, ac->mylib->mysql_error(con), 255);
		prometheus_metric_name_normalizer(reason, reason_size);
		metric_add_labels2("mysql_error", &val, DATATYPE_UINT, carg, "name",  carg->name, "reason", reason);
		metric_add_labels6("alligator_connect_ok", &unval, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", "mysql", "name", carg->name);
	    metric_add_labels6("alligator_parser_status", &unval, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", "mysql", "name", carg->name);

		ac->mylib->mysql_close(con);
		return;
	}  

	metric_add_labels6("alligator_connect_ok", &val, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", "mysql", "name", carg->name);
    carg->parser_status = 1;

	if (data->type == MY_TYPE_MYSQL)
	{
		if (carg->name)
		{
			query_ds *qds = query_get(carg->name);
			if (carg->log_level > 1)
				printf("found queries for datasource: %s: %p\n", carg->name, qds);
			if (qds)
			{
				//carg->data = con;
				alligator_ht_foreach_arg(qds->hash, mysql_queries_foreach, carg);
			}
		}

		if (!carg->data_lock)
			mysql_get_databases(con, carg);
	}

	if (data->type == MY_TYPE_SPHINXSEARCH)
	{
		sphinxsearch_queries(carg);
	}

	ac->mylib->mysql_close(con);
	metric_add_labels6("alligator_parser_status", &carg->parser_status, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", "mysql", "name", carg->name);
}

void mysql_timer(void *arg) {
	extern aconf* ac;
	usleep(ac->aggregator_startup * 1000);
	while ( 1 )
	{
		puts("alligator_ht_foreach");
		alligator_ht_foreach(ac->my_aggregator, mysql_run);
		usleep(ac->aggregator_repeat * 1000);
	}
}

void mysql_timer_without_thread(uv_timer_t* handle) {
	(void)handle;
	alligator_ht_foreach(ac->my_aggregator, mysql_run);
}

void mysql_without_thread()
{
	uv_timer_init(ac->loop, &ac->my_timer);
	uv_timer_start(&ac->my_timer, mysql_timer_without_thread, ac->aggregator_startup, ac->aggregator_repeat);
}

void mysql_client_handler()
{
	// uncomment for thread
	//uv_thread_t th;
	//uv_thread_create(&th, mysql_timer, NULL);
	mysql_without_thread();
}

char* mysql_client(context_arg* carg)
{
	if (!carg)
		return NULL;

	carg->key = malloc(255);
	snprintf(carg->key, 255, "%s", carg->host);

	alligator_ht_insert(ac->my_aggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));
	return "mysql";
}

void mysql_client_del(context_arg* carg)
{
	if (!carg)
		return;

	alligator_ht_remove_existing(ac->my_aggregator, &(carg->node));
	carg_free(carg);
}

void mysql_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("mysql");
	actx->data = calloc(1, sizeof(my_data));
	my_data *data = actx->data;
	data->type = MY_TYPE_MYSQL;
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = NULL;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = NULL;
	strlcpy(actx->handler[0].key, "mysql", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

void sphinxsearch_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("sphinxsearch");
	actx->data = calloc(1, sizeof(my_data));
	my_data *data = actx->data;
	data->type = MY_TYPE_SPHINXSEARCH;
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = NULL;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = NULL;
	strlcpy(actx->handler[0].key, "sphinxsearch", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
