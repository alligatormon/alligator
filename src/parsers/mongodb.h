#pragma once
#include <bson.h>
#include <mongoc.h>
typedef struct libmongo
{
	uv_lib_t* lib_mongoc_client_command_simple;
	bool (*mongoc_client_command_simple) (mongoc_client_t *client, const char *db_name, const bson_t *command, const mongoc_read_prefs_t *read_prefs, bson_t *reply, bson_error_t *error);

	uv_lib_t* lib_mongoc_collection_command_simple;
	bool (*mongoc_collection_command_simple) (mongoc_collection_t *collection, const bson_t *command, const mongoc_read_prefs_t *read_prefs, bson_t *reply, bson_error_t *error);

	uv_lib_t* lib_mongoc_collection_find_with_opts;
	mongoc_cursor_t* (*mongoc_collection_find_with_opts) (mongoc_collection_t *collection, const bson_t *filter, const bson_t *opts, const mongoc_read_prefs_t *read_prefs);

	uv_lib_t* lib_mongoc_collection_find;
	mongoc_cursor_t* (*mongoc_collection_find) (mongoc_collection_t *collection, mongoc_query_flags_t flags, uint32_t skip, uint32_t limit, uint32_t batch_size, const bson_t *query, const bson_t *fields, const mongoc_read_prefs_t *read_prefs);

	uv_lib_t* lib_mongoc_cursor_next;
	bool (*mongoc_cursor_next) (mongoc_cursor_t *cursor, const bson_t **bson);

	uv_lib_t* lib_mongoc_cursor_destroy;
	void (*mongoc_cursor_destroy) (mongoc_cursor_t *cursor);

	uv_lib_t* lib_mongoc_database_find_collections_with_opts;
	mongoc_cursor_t* (*mongoc_database_find_collections_with_opts) (mongoc_database_t *database, const bson_t *opts);

	uv_lib_t* lib_mongoc_database_find_collections;
	mongoc_cursor_t* (*mongoc_database_find_collections) (mongoc_database_t *database, const bson_t *filter, bson_error_t *error);

	uv_lib_t* lib_mongoc_collection_destroy;
	void (*mongoc_collection_destroy) (mongoc_collection_t *collection);

	uv_lib_t* lib_mongoc_client_get_collection;
	mongoc_collection_t* (*mongoc_client_get_collection) (mongoc_client_t *client, const char *db, const char *collection);

	uv_lib_t* lib_mongoc_cursor_error;
	bool (*mongoc_cursor_error) (mongoc_cursor_t *cursor, bson_error_t *error);

	uv_lib_t* lib_mongoc_client_get_database_names_with_opts;
	char ** (*mongoc_client_get_database_names_with_opts) (mongoc_client_t *client, const bson_t *opts, bson_error_t *error);

	uv_lib_t* lib_mongoc_client_get_database_names;
	char ** (*mongoc_client_get_database_names) (mongoc_client_t *client, bson_error_t *error);

	uv_lib_t* lib_mongoc_client_get_database;
	mongoc_database_t* (*mongoc_client_get_database) (mongoc_client_t *client, const char *name);

	uv_lib_t* lib_mongoc_database_destroy;
	void (*mongoc_database_destroy)(mongoc_database_t *database);

	uv_lib_t* lib_mongoc_init;
	void (*mongoc_init) ();

	uv_lib_t* lib_mongoc_client_new;
	mongoc_client_t* (*mongoc_client_new) (const char *uri_string);

	uv_lib_t* lib_mongoc_client_destroy;
	void (*mongoc_client_destroy) (mongoc_client_t *client);

	uv_lib_t* lib_mongoc_cleanup;
	void (*mongoc_cleanup) ();

	//uv_lib_t* lib_mongoc_uri_new_with_error;
	//mongoc_uri_t* (*mongoc_uri_new_with_error) (const char *uri_string, bson_error_t *error);

	//uv_lib_t* lib_mongoc_client_new_from_uri;
	//mongoc_client_t* (*mongoc_client_new_from_uri) (const mongoc_uri_t *uri);
} libmongo;
