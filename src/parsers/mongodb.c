#include <stdio.h>
#include "main.h"
#include "parsers/mongodb.h"
#include "modules/modules.h"
extern aconf* ac;

void mongo_cmd_run(mongoc_collection_t *collection, mongoc_client_t *client, char *db_name, char *cmd, char *arg)
{
	bson_t *command;
	bson_error_t error;
	bson_t reply;
	char *str;
	command = BCON_NEW (cmd, BCON_UTF8 (arg));
	if ((collection) && ac->libmongo->mongoc_collection_command_simple ( collection, command, NULL, &reply, &error)) {
		str = bson_as_canonical_extended_json (&reply, NULL);
		//printf ("======= run %s: =========\n%s\n", cmd, str);
		bson_free (str);
	} else if ((!collection) && ac->libmongo->mongoc_client_command_simple(client, db_name, command, NULL, &reply, &error)) {
		str = bson_as_canonical_extended_json (&reply, NULL);
		//printf ("======= run %s (%s): =========\n%s\n", cmd, db_name, str);
		bson_free (str);
	} else {
		fprintf (stderr, "Failed to run command: %s\n", error.message);
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

	mongoc_cursor_t *cursor = ac->libmongo->mongoc_collection_find_with_opts (collection, query, opts, NULL);

	const bson_t *doc;
	char *str;
	while (ac->libmongo->mongoc_cursor_next(cursor, &doc)) {
		str = bson_as_canonical_extended_json (doc, NULL);
		printf("\t\t\t%s\n", str);
		bson_free(str);
	}

	bson_destroy(query);
	ac->libmongo->mongoc_cursor_destroy(cursor);
	puts("---------------------------");
}


void mongo_get_collections(mongoc_database_t *database, char *db, mongoc_client_t *client)
{
	bson_t opts = BSON_INITIALIZER;
	bson_t name_filter;
	const bson_t *doc;
	bson_iter_t iter;
	bson_error_t error;

	BSON_APPEND_DOCUMENT_BEGIN (&opts, "filter", &name_filter);
	/* find collections with names like "abbbbc" */
	BSON_APPEND_REGEX (&name_filter, "name", "", NULL);
	bson_append_document_end (&opts, &name_filter);

	mongoc_cursor_t *cursor = ac->libmongo->mongoc_database_find_collections_with_opts (database, &opts);
	while (ac->libmongo->mongoc_cursor_next (cursor, &doc)) {
		bson_iter_init_find (&iter, doc, "name");
		char *collection_name = (char*)bson_iter_utf8 (&iter, NULL);
		mongoc_collection_t *collection = ac->libmongo->mongoc_client_get_collection (client, db, bson_iter_utf8 (&iter, NULL));
		printf ("============ found collection: %s\n", bson_iter_utf8 (&iter, NULL));

		if (!strcmp(collection_name, "products"))
		{
			mongo_find(client, collection, "{\"item\" : \"stamps\"}", NULL);
			//mongo_find(client, collection, "{\"item\" : \"stamps\"}", "{ \"item\": 1 }");
		}

		mongo_cmd_run(collection, NULL, "", "collStats", (char*)bson_iter_utf8 (&iter, NULL));

		ac->libmongo->mongoc_collection_destroy (collection);
	}

	if (ac->libmongo->mongoc_cursor_error (cursor, &error)) {
		fprintf (stderr, "%s\n", error.message);
	}

	ac->libmongo->mongoc_cursor_destroy (cursor);
	bson_destroy (&opts);
}

void mongo_get_databases(mongoc_client_t *client)
{
	bson_error_t error;
	char **strv;
	unsigned i;

	if ((strv = ac->libmongo->mongoc_client_get_database_names_with_opts (client, NULL, &error))) {
		for (i = 0; strv[i]; i++)
		{
			puts("============== database =================");
			printf ("= %s\n", strv[i]);
			mongoc_database_t *database = ac->libmongo->mongoc_client_get_database (client, strv[i]);

			mongo_get_collections(database, strv[i], client);
		 }

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
	if (lm && lm->mongoc_cursor_next) free(lm->mongoc_cursor_next);
	if (lm && lm->mongoc_cursor_destroy) free(lm->mongoc_cursor_destroy);
	if (lm && lm->mongoc_database_find_collections_with_opts) free(lm->mongoc_database_find_collections_with_opts);
	if (lm && lm->mongoc_collection_destroy) free(lm->mongoc_collection_destroy);
	if (lm && lm->mongoc_client_get_collection) free(lm->mongoc_client_get_collection);
	if (lm && lm->mongoc_cursor_error) free(lm->mongoc_cursor_error);
	if (lm && lm->mongoc_client_get_database_names_with_opts) free(lm->mongoc_client_get_database_names_with_opts);
	if (lm && lm->mongoc_client_get_database) free(lm->mongoc_client_get_database);
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
		printf("Cannot get mongoc_collection_find_with_opts from mongolib\n");
		mongolib_free(lm);
		return NULL;
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
		mongolib_free(lm);
		return NULL;
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
		printf("Cannot get mongoc_client_get_database_names_with_opts from mongolib\n");
		mongolib_free(lm);
		return NULL;
	}

	*(void**)(&lm->mongoc_client_get_database) = module_load(lmongo->path, "mongoc_client_get_database", &lm->lib_mongoc_client_get_database);
	if (!lm->mongoc_client_get_database)
	{
		printf("Cannot get mongoc_client_get_database from mongolib\n");
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

void mongodb_parser()
{
	mongoc_client_t *client;

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

	client = ac->libmongo->mongoc_client_new ("mongodb://localhost:27017/?appname=executing-example");
	puts("===============================");
	mongo_get_databases(client);
	puts("===============================");

	mongo_cmd_run(NULL, client, "admin", "serverStatus", "");
	//mongo_cmd_run(collection, "dbStats", "");
	mongo_cmd_run(NULL, client, "admin", "replSetGetStatus", "");

	ac->libmongo->mongoc_client_destroy (client);
	ac->libmongo->mongoc_cleanup ();

	return;
}

