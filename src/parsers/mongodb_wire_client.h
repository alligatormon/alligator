#pragma once

#include <stddef.h>
#include <jansson.h>

typedef struct mongodb_wire_client mongodb_wire_client;

typedef int (*mongodb_wire_doc_cb)(json_t *doc, void *arg);

int mongodb_wire_client_open(mongodb_wire_client **out, const char *uri, char *err, size_t errsz);
void mongodb_wire_client_close(mongodb_wire_client *client);

int mongodb_wire_list_databases(mongodb_wire_client *client, char ***names, size_t *count, char *err, size_t errsz);
int mongodb_wire_list_collections(mongodb_wire_client *client, const char *dbname, char ***names, size_t *count, char *err, size_t errsz);
int mongodb_wire_find(mongodb_wire_client *client, const char *dbname, const char *collection, const char *filter_json, mongodb_wire_doc_cb cb, void *arg, char *err, size_t errsz);

void mongodb_wire_free_string_list(char **names, size_t count);
