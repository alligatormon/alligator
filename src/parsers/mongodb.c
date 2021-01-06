#include <stdio.h>
#include "main.h"
#include "parsers/mongodb.h"
#include "modules/modules.h"
extern aconf* ac;

void mongo_add_metric(context_arg *carg, char *key1, char *key2, char *key3, char *key, char *name, char *ns, void* val, int8_t type)
{
	tommy_hashdyn *hash = malloc(sizeof(*hash));
	tommy_hashdyn_init(hash);
	if (ns)
		labels_hash_insert_nocache(hash, "namespace", ns);
	if (key1)
		labels_hash_insert_nocache(hash, "name", key1);
	if (key2)
		labels_hash_insert_nocache(hash, "key", key2);
	if (key3)
		labels_hash_insert_nocache(hash, "desc", key3);

	metric_add(name, hash, val, type, carg);
}

int mongo_get_value(context_arg *carg, json_t *value, char *key1, char *key2, char *key3, char *key, char *name, char *ns, int in)
{
	int type = json_typeof(value);
	if (carg->log_level > 2)
		printf("%d===== name: %s, key: %s, key1: %s, key2:%s, key3: %s, type %d\n", in, name, key, key1, key2, key3, type);
	
	if (type == JSON_REAL)
	{
		double dl = json_real_value(value);
		if (carg->log_level > 2)
			printf("ns: %s, metric: %s, TYPE: %s, name: %s, key:%s, desc: %s, value: %lf\n", ns, name, key, key1, key2, key3, dl);

		mongo_add_metric(carg, key1, key2, key3, key, name, ns, &dl, DATATYPE_DOUBLE);
	}
	else if (type == JSON_INTEGER)
	{
		int64_t vl = json_integer_value(value);
		if (carg->log_level > 2)
			printf("ns: %s, metric: %s, TYPE: %s, name: %s, key:%s, desc: %s, value: %"d64"\n", ns, name, key, key1, key2, key3, vl);

		mongo_add_metric(carg, key1, key2, key3, key, name, ns, &vl, DATATYPE_INT);
	}
	else if (type == JSON_STRING)
	{
		char *svl = (char*)json_string_value(value);
		if (strcmp(key, "$numberInt"))
		{
			uint64_t vl = strtoll(svl, NULL, 10);
			if (carg->log_level > 2)
				printf("ns: %s, metric: %s, TYPE: %s, name: %s, key:%s, desc: %s, value: %"d64"\n", ns, name, key, key1, key2, key3, vl);

			mongo_add_metric(carg, key1, key2, key3, key, name, ns, &vl, DATATYPE_INT);
		}
		else if (strcmp(key, "$numberDouble"))
		{
			double dl = strtod(svl, NULL);
			if (carg->log_level > 2)
				printf("ns: %s, metric: %s, TYPE: %s, name: %s, key:%s, desc: %s, value: %lf\n", ns, name, key, key1, key2, key3, dl);

			mongo_add_metric(carg, key1, key2, key3, key, name, ns, &dl, DATATYPE_DOUBLE);
		}
		else if (strcmp(key, "$clusterTime"))
		{
			uint64_t vl = strtoll(svl, NULL, 10);
			if (carg->log_level > 2)
				printf("ns: %s, metric: %s, TYPE: %s, name: %s, key:%s, desc: %s, value: %"d64"\n", ns, name, key, key1, key2, key3, vl);

			mongo_add_metric(carg, key1, key2, key3, key, name, ns, &vl, DATATYPE_INT);
		}
	}

	return type;
}

void mongo_Stats(context_arg *carg, char *metrics, char *prefix, char *ns_override)
{
	if (carg->log_level > 2)
	{
		puts("===========================");
		puts(metrics);
		puts("===========================");
	}
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		if (carg->log_level > 1)
			fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	char *ns = ns_override;
	if (!ns_override)
	{
		json_t *ns_json = json_object_get(root, "ns");
		ns = (char*)json_string_value(ns_json);
	}

	const char *key;
	json_t *value1;
	char string[255];
	size_t string_len;
	snprintf(string,  254, "MongoDB_%s", prefix);
	size_t fsize = strlen(string);
	json_object_foreach(root, key, value1)
	{
		string_len = strlen(key);
		if (key[string_len-1] == '_')
			--string_len;

		if (*key != '$')
		{
			string[fsize] = '_';
			strlcpy(string+1+fsize, key, string_len+1);
		}
		int type = json_typeof(value1);

		if (type == JSON_OBJECT)
		{
			const char *key2;
			json_t *value2;
			json_object_foreach(value1, key2, value2)
			{
				int type2 = mongo_get_value(carg, value2, (char*)key, NULL, NULL, (char*)key2, string, ns, 1);

				if (type2 == JSON_OBJECT)
				{
					const char *key3;
					json_t *value3;
					json_object_foreach(value2, key3, value3)
					{
						int type3 = mongo_get_value(carg, value3, (char*)key2, NULL, NULL, (char*)key3, string, ns, 2);
						if (type3 == JSON_OBJECT)
						{
							const char *key4;
							json_t *value4;
							json_object_foreach(value3, key4, value4)
							{
								int type4 = mongo_get_value(carg, value4, (char*)key2, (char*)key3, NULL, (char*)key4, string, ns, 3);
								if (type4 == JSON_OBJECT)
								{
									const char *key5;
									json_t *value5;
									json_object_foreach(value4, key5, value5)
									{
										mongo_get_value(carg, value5, (char*)key2, (char*)key3, (char*)key4, (char*)key5, string, ns, 4);
									}
								}



							}
						}
					}
				}
			}
		}
	}
	json_decref(root);
}

void mongo_cmd_run(context_arg *carg, mongoc_collection_t *collection, mongoc_client_t *client, char *db_name, char *cmd, char *arg, void (*callback)(context_arg *carg, char *str, char *prefix, char *ns_override), char *prefix, char *ns_override)
{
	if (carg->log_level > 1) 
		printf("mongo_cmd_run(carg %p, collection %p, client %p, db_name %s, cmd %s, arg %s\n", carg, collection, client, db_name, cmd, arg);
	bson_t *command;
	bson_error_t error;
	bson_t reply;
	char *str;
	command = BCON_NEW (cmd, BCON_UTF8 (arg));
	if ((collection) && ac->libmongo->mongoc_collection_command_simple ( collection, command, NULL, &reply, &error)) {
		//str = bson_as_canonical_extended_json (&reply, NULL);
		str = bson_as_json (&reply, NULL);
		if (carg->log_level > 2) 
			printf ("======= run %s: =========\n%s\n", cmd, str);

		if (callback)
			callback(carg, str, prefix, ns_override);

		bson_free (str);
	} else if ((!collection) && ac->libmongo->mongoc_client_command_simple(client, db_name, command, NULL, &reply, &error)) {
		//str = bson_as_canonical_extended_json (&reply, NULL);
		str = bson_as_json (&reply, NULL);
		if (carg->log_level > 2) 
			printf ("======= run %s (%s): =========\n%s\n", cmd, db_name, str);

		if (callback)
			callback(carg, str, prefix, ns_override);

		bson_free (str);
	} else {
		if (carg->log_level > 1)
			fprintf (stderr, "Failed to run command: %s, error: %s\n", cmd, error.message);
	}

	bson_destroy (command);
	bson_destroy (&reply);
}

void mongo_find(mongoc_client_t *client, mongoc_collection_t *collection, char *json_query, char *json_opts)
{
	puts("+++++++++++++++++++++++++++++++++++++");
	printf("find '%s' with opts '%s'\n", json_query, json_opts);
	bson_error_t error;
	bson_t *query = bson_new_from_json ((const uint8_t *)json_query, -1, &error);

	bson_t *opts = NULL;
	if (json_opts)
		opts = bson_new_from_json ((const uint8_t *)json_opts, -1, &error);

	//mongoc_cursor_t *cursor = ac->libmongo->mongoc_collection_find_with_opts (collection, query, opts, NULL);
	mongoc_cursor_t *cursor;
	if (ac->libmongo->mongoc_collection_find_with_opts)
	{
		cursor = ac->libmongo->mongoc_collection_find_with_opts (collection, query, opts, NULL);
	}
	else if (ac->libmongo->mongoc_collection_find)
	{
		cursor = ac->libmongo->mongoc_collection_find (collection, 0, 0, 0, 0, query, opts, NULL);
	}
	else
	{
		printf("mongodb error: mongoc_collection_find_with_opts or mongoc_collection_find_with_opts not loaded from mongo-c-driver module.\n");
		return;
	}

	const bson_t *doc;
	char *str;
	while (ac->libmongo->mongoc_cursor_next(cursor, &doc)) {
		//str = bson_as_canonical_extended_json (doc, NULL);
		str = bson_as_json (doc, NULL);
		printf("\t\t\t%s\n", str);
		bson_free(str);
	}

	bson_destroy(query);
	ac->libmongo->mongoc_cursor_destroy(cursor);
	puts("---------------------------");
}


void mongo_get_collections(context_arg *carg, mongoc_database_t *database, char *db, mongoc_client_t *client)
{
	bson_t opts = BSON_INITIALIZER;
	bson_t name_filter;
	const bson_t *doc;
	bson_iter_t iter;
	bson_error_t error;

	BSON_APPEND_DOCUMENT_BEGIN (&opts, "filter", &name_filter);
	BSON_APPEND_REGEX (&name_filter, "name", "", NULL);
	bson_append_document_end (&opts, &name_filter);

	mongoc_cursor_t *cursor = NULL;
	if (ac->libmongo->mongoc_database_find_collections_with_opts)
	{
		cursor = ac->libmongo->mongoc_database_find_collections_with_opts (database, &opts);
	}
	else if (ac->libmongo->mongoc_database_find_collections)
	{
		cursor = ac->libmongo->mongoc_database_find_collections (database, &opts, NULL);
	}
	else
	{
		if (carg->log_level > 0)
			printf("mongodb error: not loaded mongoc_database_find_collections_with_opts or mongoc_database_find_collections from mongo-c-driver.\n");
		return;
	}
	while (ac->libmongo->mongoc_cursor_next (cursor, &doc)) {
		bson_iter_init_find (&iter, doc, "name");
		char *collection_name = (char*)bson_iter_utf8 (&iter, NULL);
		mongoc_collection_t *collection = ac->libmongo->mongoc_client_get_collection (client, db, bson_iter_utf8 (&iter, NULL));
		if (carg->log_level > 1)
			printf ("============ found collection: %s\n", bson_iter_utf8 (&iter, NULL));

		//if (!strcmp(collection_name, "p"))
		//{
		//	mongo_find(client, collection, "{\"ping\" : 1}", NULL);
		//	//mongo_find(client, collection, "{\"item\" : \"stamps\"}", "{ \"item\": 1 }");
		//}

		mongo_cmd_run(carg, collection, NULL, "", "collStats", (char*)bson_iter_utf8 (&iter, NULL), mongo_Stats, "Collection", NULL);
		mongo_cmd_run(carg, collection, NULL, "", "dataSize", (char*)bson_iter_utf8 (&iter, NULL), mongo_Stats, "DataSize", collection_name);
		ac->libmongo->mongoc_collection_destroy (collection);
	}

	if (ac->libmongo->mongoc_cursor_error (cursor, &error)) {
		fprintf (stderr, "%s\n", error.message);
	}

	ac->libmongo->mongoc_cursor_destroy (cursor);
	bson_destroy (&opts);
}

void mongo_get_databases(mongoc_client_t *client, context_arg *carg)
{
	bson_error_t error;
	char **strv;
	unsigned i;

	if (ac->libmongo->mongoc_client_get_database_names_with_opts)
		strv = ac->libmongo->mongoc_client_get_database_names_with_opts (client, NULL, &error);
	else if (ac->libmongo->mongoc_client_get_database_names)
		strv = ac->libmongo->mongoc_client_get_database_names(client, &error);
	else
	{
		if (carg->log_level > 0)
			printf("mongoc_client_get_database_names_with_opts or mongoc_client_get_database_names not loaded from mongo-c-library\n");
		return;
	}
	if (strv)
	{
		for (i = 0; strv[i]; i++)
		{
			if (carg->log_level > 1)
			{
				puts("============== database =================");
				printf ("= %s\n", strv[i]);
			}
			mongoc_database_t *database = ac->libmongo->mongoc_client_get_database (client, strv[i]);
	
			mongo_cmd_run(carg, NULL, client, strv[i], "connPoolStats", "", mongo_Stats, "ConnectionPoolStatus", strv[i]);
			mongo_cmd_run(carg, NULL, client, strv[i], "dbStats", "", mongo_Stats, "DBStats", strv[i]);
			mongo_cmd_run(carg, NULL, client, strv[i], "features", "", mongo_Stats, "Features", strv[i]);
			mongo_cmd_run(carg, NULL, client, strv[i], "serverStatus", "", mongo_Stats, "ServerStatus", strv[i]);

			mongo_get_collections(carg, database, strv[i], client);
			ac->libmongo->mongoc_database_destroy(database);
		}
		mongo_cmd_run(carg, NULL, client, "admin", "buildInfo", "", mongo_Stats, "Build", "admin");
		mongo_cmd_run(carg, NULL, client, "admin", "top", "", mongo_Stats, "Top", "admin");
		mongo_cmd_run(carg, NULL, client, "admin", "isMaster", "", mongo_Stats, "Master", "admin");
		mongo_cmd_run(carg, NULL, client, "admin", "replSetGetStatus", "", mongo_Stats, "ReplicationStatus", "admin");

		bson_strfreev (strv);
	} else {
		fprintf (stderr, "Command failed: %s\n", error.message);
	}
}

void mongolib_free(libmongo *lm)
{
	if (lm && lm->mongoc_init) free(lm->mongoc_init);
	if (lm && lm->mongoc_client_command_simple) free(lm->mongoc_client_command_simple);
	if (lm && lm->mongoc_collection_find_with_opts) free(lm->mongoc_collection_find_with_opts);
	if (lm && lm->mongoc_collection_find) free(lm->mongoc_collection_find);
	if (lm && lm->mongoc_cursor_next) free(lm->mongoc_cursor_next);
	if (lm && lm->mongoc_cursor_destroy) free(lm->mongoc_cursor_destroy);
	if (lm && lm->mongoc_database_find_collections_with_opts) free(lm->mongoc_database_find_collections_with_opts);
	if (lm && lm->mongoc_database_find_collections) free(lm->mongoc_database_find_collections);
	if (lm && lm->mongoc_collection_destroy) free(lm->mongoc_collection_destroy);
	if (lm && lm->mongoc_client_get_collection) free(lm->mongoc_client_get_collection);
	if (lm && lm->mongoc_cursor_error) free(lm->mongoc_cursor_error);
	if (lm && lm->mongoc_client_get_database_names_with_opts) free(lm->mongoc_client_get_database_names_with_opts);
	if (lm && lm->mongoc_client_get_database_names) free(lm->mongoc_client_get_database_names);
	if (lm && lm->mongoc_client_get_database) free(lm->mongoc_client_get_database);
	if (lm && lm->mongoc_database_destroy) free(lm->mongoc_database_destroy);
	if (lm && lm->mongoc_init) free(lm->mongoc_init);
	if (lm && lm->mongoc_client_new) free(lm->mongoc_client_new);
	if (lm && lm->mongoc_client_destroy) free(lm->mongoc_client_destroy);
	if (lm && lm->mongoc_cleanup) free(lm->mongoc_cleanup);
	if (lm) free(lm);
}

libmongo *mongodb_init()
{
	module_t *lmongo = tommy_hashdyn_search(ac->modules, module_compare, "mongodb", tommy_strhash_u32(0, "mongodb"));
	if (!lmongo)
	{
		printf("No defined mongodb library in configuration\n");
		return NULL;
	}


	libmongo *lm = calloc(1, sizeof(*lm));

	*(void**)(&lm->mongoc_init) = module_load(lmongo->path, "mongoc_init", &lm->lib_mongoc_init);
	if (!lm->mongoc_init)
	{
		printf("Cannot get mongoc_init from mongolib\n");
		mongolib_free(lm);
		return NULL;
	}

	*(void**)(&lm->mongoc_client_command_simple) = module_load(lmongo->path, "mongoc_client_command_simple", &lm->lib_mongoc_client_command_simple);
	if (!lm->mongoc_client_command_simple)
	{
		printf("Cannot get mongoc_client_command_simple from mongolib\n");
		mongolib_free(lm);
		return NULL;
	}

	*(void**)(&lm->mongoc_collection_command_simple) = module_load(lmongo->path, "mongoc_collection_command_simple", &lm->lib_mongoc_collection_command_simple);
	if (!lm->mongoc_collection_command_simple)
	{
		printf("Cannot get mongoc_collection_command_simple from mongolib\n");
		mongolib_free(lm);
		return NULL;
	}

	*(void**)(&lm->mongoc_collection_find_with_opts) = module_load(lmongo->path, "mongoc_collection_find_with_opts", &lm->lib_mongoc_collection_find_with_opts);
	if (!lm->mongoc_collection_find_with_opts)
	{
		printf("Cannot get mongoc_collection_find_with_opts, use mongoc_collection_find instead of\n");
	}

	*(void**)(&lm->mongoc_collection_find) = module_load(lmongo->path, "mongoc_collection_find", &lm->lib_mongoc_collection_find);
	if (!lm->mongoc_collection_find)
	{
		printf("Cannot get mongoc_collection_find from mongolib\n");
	}

	*(void**)(&lm->mongoc_cursor_next) = module_load(lmongo->path, "mongoc_cursor_next", &lm->lib_mongoc_cursor_next);
	if (!lm->mongoc_cursor_next)
	{
		printf("Cannot get mongoc_cursor_next from mongolib\n");
		mongolib_free(lm);
		return NULL;
	}

	*(void**)(&lm->mongoc_cursor_destroy) = module_load(lmongo->path, "mongoc_cursor_destroy", &lm->lib_mongoc_cursor_destroy);
	if (!lm->mongoc_cursor_destroy)
	{
		printf("Cannot get mongoc_cursor_destroy from mongolib\n");
		mongolib_free(lm);
		return NULL;
	}

	*(void**)(&lm->mongoc_database_find_collections_with_opts) = module_load(lmongo->path, "mongoc_database_find_collections_with_opts", &lm->lib_mongoc_database_find_collections_with_opts);
	if (!lm->mongoc_database_find_collections_with_opts)
	{
		printf("Cannot get mongoc_database_find_collections_with_opts from mongolib\n");
	}

	*(void**)(&lm->mongoc_database_find_collections) = module_load(lmongo->path, "mongoc_database_find_collections", &lm->lib_mongoc_database_find_collections);
	if (!lm->mongoc_database_find_collections)
	{
		printf("Cannot get mongoc_database_find_collections_with_opts from mongolib, use mongoc_database_find_collections instead of\n");
	}

	*(void**)(&lm->mongoc_collection_destroy) = module_load(lmongo->path, "mongoc_collection_destroy", &lm->lib_mongoc_collection_destroy);
	if (!lm->mongoc_collection_destroy)
	{
		printf("Cannot get mongoc_collection_destroy from mongolib\n");
		mongolib_free(lm);
		return NULL;
	}

	*(void**)(&lm->mongoc_client_get_collection) = module_load(lmongo->path, "mongoc_client_get_collection", &lm->lib_mongoc_client_get_collection);
	if (!lm->mongoc_client_get_collection)
	{
		printf("Cannot get mongoc_client_get_collection from mongolib\n");
		mongolib_free(lm);
		return NULL;
	}

	*(void**)(&lm->mongoc_cursor_error) = module_load(lmongo->path, "mongoc_cursor_error", &lm->lib_mongoc_cursor_error);
	if (!lm->mongoc_cursor_error)
	{
		printf("Cannot get mongoc_cursor_error from mongolib\n");
		mongolib_free(lm);
		return NULL;
	}

	*(void**)(&lm->mongoc_client_get_database_names_with_opts) = module_load(lmongo->path, "mongoc_client_get_database_names_with_opts", &lm->lib_mongoc_client_get_database_names_with_opts);
	if (!lm->mongoc_client_get_database_names_with_opts)
	{
		printf("Cannot get mongoc_client_get_database_names_with_opts from mongolib, use mongoc_client_get_database_names instead of\n");
	}

	*(void**)(&lm->mongoc_client_get_database_names) = module_load(lmongo->path, "mongoc_client_get_database_names", &lm->lib_mongoc_client_get_database_names);
	if (!lm->mongoc_client_get_database_names)
	{
		printf("Cannot get mongoc_client_get_database_names from mongolib\n");
	}

	*(void**)(&lm->mongoc_client_get_database) = module_load(lmongo->path, "mongoc_client_get_database", &lm->lib_mongoc_client_get_database);
	if (!lm->mongoc_client_get_database)
	{
		printf("Cannot get mongoc_client_get_database from mongolib\n");
		mongolib_free(lm);
		return NULL;
	}

	*(void**)(&lm->mongoc_database_destroy) = module_load(lmongo->path, "mongoc_database_destroy", &lm->lib_mongoc_database_destroy);
	if (!lm->mongoc_client_get_database)
	{
		printf("Cannot get mongoc_database_destroy from mongolib\n");
		mongolib_free(lm);
		return NULL;
	}

	*(void**)(&lm->mongoc_client_new) = module_load(lmongo->path, "mongoc_client_new", &lm->lib_mongoc_client_new);
	if (!lm->mongoc_client_new)
	{
		printf("Cannot get mongoc_client_new from mongolib\n");
		mongolib_free(lm);
		return NULL;
	}

	*(void**)(&lm->mongoc_client_destroy) = module_load(lmongo->path, "mongoc_client_destroy", &lm->lib_mongoc_client_destroy);
	if (!lm->mongoc_client_destroy)
	{
		printf("Cannot get mongoc_client_destroy from mongolib\n");
		mongolib_free(lm);
		return NULL;
	}

	*(void**)(&lm->mongoc_cleanup) = module_load(lmongo->path, "mongoc_cleanup", &lm->lib_mongoc_cleanup);
	if (!lm->mongoc_cleanup)
	{
		printf("Cannot get mongoc_cleanup from mongolib\n");
		mongolib_free(lm);
		return NULL;
	}

	return lm;
}

void mongodb_run(void* arg)
{
	mongoc_client_t *client;
	//bson_error_t error;

	context_arg *carg = arg;

	if (!ac->libmongo)
	{
		libmongo *lm = mongodb_init();
		if (lm)
			ac->libmongo = lm;
		else
		{
			puts("Cannot initialize mongodb functions via module");
			return;
		}
	}

	ac->libmongo->mongoc_init ();

	client = ac->libmongo->mongoc_client_new (carg->url);
	mongo_get_databases(client, carg);

	ac->libmongo->mongoc_client_destroy (client);
	ac->libmongo->mongoc_cleanup ();

	return;
}

void mongodb_timer(void *arg) {
	extern aconf* ac;
	usleep(ac->aggregator_startup * 1000);
	while ( 1 )
	{
		tommy_hashdyn_foreach(ac->mongodb_aggregator, mongodb_run);
		usleep(ac->aggregator_repeat * 1000);
	}
}

void mongodb_timer_without_thread(uv_timer_t* handle) {
	(void)handle;
	tommy_hashdyn_foreach(ac->mongodb_aggregator, mongodb_run);
}

void mongodb_without_thread()
{
	uv_timer_t *timer1 = calloc(1, sizeof(*timer1));
	uv_timer_init(ac->loop, timer1);
	uv_timer_start(timer1, mongodb_timer_without_thread, ac->aggregator_startup, ac->aggregator_repeat);
}

void mongodb_client_handler()
{
	extern aconf* ac;

	// for enable thread uncomment
	//uv_thread_t th;
	//uv_thread_create(&th, mongodb_timer, NULL);

	// uncomment for enable single threaded
	mongodb_without_thread();
}

char* mongodb_client(context_arg* carg)
{
	if (!carg)
		return NULL;

	carg->key = malloc(255);
	snprintf(carg->key, 255, "%s", carg->host);

	tommy_hashdyn_insert(ac->mongodb_aggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));
	return "mongodb";
}

void mongodb_client_del(context_arg* carg)
{
	if (!carg)
		return;

	tommy_hashdyn_remove_existing(ac->mongodb_aggregator, &(carg->node));
	carg_free(carg);
}

void mongodb_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("mongodb");
	actx->handlers = 1;
	actx->handler = malloc(sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = NULL;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = NULL;
	strlcpy(actx->handler[0].key,"mongodb", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

