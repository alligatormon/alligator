#include <stdio.h>
#include <stdlib.h>
#include <jansson.h>
#include <string.h>
#include "tommyds/tommyds/tommy.h"

#define JSON_PARSER_PRINT 0
#define JSON_PARSER_SUM 1
#define JSON_PARSER_AVG 2
#define FILEE	"tests/hadoop/datanode-stats"
#define OBJSIZE 100

typedef struct pjson_collector
{
	double collector;
	char *key;
	tommy_node node;
} pjson_collector;

typedef struct pjson_node
{
	char *key;
	char *by;
	int action;
	size_t size;
	tommy_hashdyn *hash;
	tommy_node node;
} pjson_node;

typedef struct jsonparse_kv
{
	char *k;
	char *v;
	struct jsonparse_kv *n;
} jsonparse_kv;

void selector_replace(char *str, size_t size, char from, char to)
{
	int64_t i;
	for (i=0; i<size; i++)
		if ( str[i] == from )
			str[i] = to;
}

int pjson_collector_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((pjson_collector*)obj)->key;
	return strcmp(s1, s2);
}

int pjson_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((pjson_node*)obj)->key;
	return strcmp(s1, s2);
}

void jsonparser_collector_foreach(void *funcarg, void* arg)
{
	pjson_collector *collector = arg;
	if (!collector)
		return;
	//pjson_node *node = *((pjson_node*)funcarg);
	pjson_node *node = funcarg;

	if (node->action == JSON_PARSER_AVG)
		collector->collector /= node->size;

	printf("collector %d, %s=%lf\n", node->action, collector->key, collector->collector);

	tommy_hashdyn_remove(node->hash, pjson_collector_compare, collector->key, tommy_strhash_u32(0, collector->key));
	free(collector->key);
	free(collector);
}

void print_json_aux(json_t *element, char *buf, tommy_hashdyn *hash, jsonparse_kv *kv, pjson_node *node, pjson_collector *collector);
void print_json_object(json_t *element, char *buf, tommy_hashdyn *hash, jsonparse_kv *kv, pjson_node *node);
void print_json_array(json_t *element, char *buf, tommy_hashdyn *hash, jsonparse_kv *kv);


void jsonparser_free_foreach(void* arg)
{
	pjson_node *obj = arg;
	if ( obj->hash )
		tommy_hashdyn_done(obj->hash);
	free(obj->hash);
	free(obj->key);
	free(obj);
}

void print_json_aux(json_t *element, char *buf, tommy_hashdyn *hash, jsonparse_kv *kv, pjson_node *node, pjson_collector *collector)
{
	int jsontype = json_typeof(element);
	jsonparse_kv *cur = kv;
	//switch (json_typeof(element)) {
	if (jsontype==JSON_OBJECT)
		print_json_object(element, buf, hash, kv, node);
	else if (jsontype==JSON_ARRAY)
		print_json_array(element, buf, hash, kv);
	else if (jsontype==JSON_STRING && !kv)
	{
		double af = atof(json_string_value(element));
		if ( !af )
			return;
		printf("%s: %lf\n", buf, af);
	//	print_json_string(element);
	}
	else if (jsontype==JSON_STRING && kv)
	{
		double af = atof(json_string_value(element));
		if ( !af )
			return;
		for (; cur; cur = cur->n) printf("(%s:%s) ", cur->k, cur->v);
		if (collector) collector->collector += json_integer_value(element);
		printf("%s: %lf\n", buf, af);
	}
	else if (jsontype == JSON_INTEGER && !kv)
	{
		printf("%s: \"%" JSON_INTEGER_FORMAT "\"\n", buf, json_integer_value(element));
	}
	else if (jsontype == JSON_INTEGER && kv)
	{
		for (; cur; cur = cur->n) printf("(%s:%s) ", cur->k, cur->v);
		if (collector) collector->collector += json_integer_value(element);
		printf("%s=\"%" JSON_INTEGER_FORMAT "\"\n", buf, json_integer_value(element));
	}
	else if (jsontype == JSON_REAL && !kv)
	{
		printf("%s=\"%f\"\n", buf, json_real_value(element));
	}
	else if (jsontype == JSON_REAL && kv)
	{
		for (; cur; cur = cur->n) printf("(%s:%s) ", cur->k, cur->v);
		if (collector) collector->collector += json_real_value(element);
		printf("%s=\"%f\"\n", buf, json_real_value(element));
	}
	else if (jsontype == JSON_TRUE && !kv)
	{
		printf("%s=1\n", buf);
	}
	else if (jsontype == JSON_TRUE && kv)
	{
		for (; cur; cur = cur->n) printf("(%s:%s) ", cur->k, cur->v);
		if (collector) collector->collector += 1;
		printf("%s=1\n", buf);
	}
	else if (jsontype == JSON_FALSE && !kv)
	{
		printf("%s=0\n", buf);
	}
	else if (jsontype == JSON_FALSE && kv)
	{
		for (; cur; cur = cur->n) printf("(%s:%s) ", cur->k, cur->v);
		printf("%s=0\n", buf);
	}
}

void print_json_object(json_t *element, char *buf, tommy_hashdyn *hash, jsonparse_kv *kv, pjson_node *node)
{
	const char *key;
	json_t *value;

	json_object_foreach(element, key, value)
	{
		//puts("EXPIRE");
		//if ( !node )
		//	return;
		//puts("HIT");

		//char *nby = NULL;

		//if ( kv )
		//{
		//	printf("LL '%s'\n", node->by);
		//	nby = node->by;
		//}
		char skey[OBJSIZE]; //= malloc(100);
		snprintf(skey, OBJSIZE, "%s:%s", buf, key);
		char colkey[OBJSIZE];
		snprintf(colkey, OBJSIZE, "%s", skey);
		jsonparse_kv *cur = kv;
		for (; cur && cur->n; cur = cur->n)
		{
			//strcat(colkey, "(");
			//strcat(colkey, cur->k);
			//strcat(colkey, ":");
			//strcat(colkey, cur->v);
			//strcat(colkey, ")");
			sprintf(colkey+strlen(colkey), "(%s:%s)", cur->k, cur->v);
		}
		//printf("COLKEY %s, key %s\n", colkey, key);
		//printf("%s + %s\n", buf, key);

		pjson_collector *collector = NULL;
		if (node && node->hash)
		{
			collector = tommy_hashdyn_search(node->hash, pjson_collector_compare, colkey, tommy_strhash_u32(0, colkey));
			if (!collector)
			{
				collector = malloc(sizeof(*collector));
				collector->collector = 0;
				collector->key = strndup(colkey, OBJSIZE);
				tommy_hashdyn_insert(node->hash, &(collector->node), collector, tommy_strhash_u32(0, collector->key));
			}
		}
		print_json_aux(value, skey, hash, kv, NULL, collector);
	}

}

void print_json_array(json_t *element, char *buf, tommy_hashdyn *hash, jsonparse_kv *root_kv)
{
	size_t i;
	size_t size = json_array_size(element);

	printf("!!!!!!!!!!!!!!!!!!!!!!!!!! %p\n", root_kv);
	pjson_node *node = NULL;
	printf("finding %s\n", buf);
	node = tommy_hashdyn_search(hash, pjson_compare, buf, tommy_strhash_u32(0, buf));
	printf("result: %p\n", node);
	if (!node)
	{
		puts("1EXIT");
		return;
	}
	jsonparse_kv *kv = NULL;
	node->size = size;
	for (i = 0; i < size; i++)
	{
		int kvflag = 0;
		jsonparse_kv *cur = root_kv;
		json_t *arr_obj = json_array_get(element, i);
		if ( json_typeof(arr_obj) == JSON_OBJECT )
		{
			kv = NULL;

			printf("ffinding '%s'\n", node->by);
			json_t *objobj = json_object_get(arr_obj, node->by);
			if (!objobj)
			{
				puts("2EXIT");
				return;
			}
			//printf("result objobj %p\n", objobj);
			kv = malloc(sizeof(*kv));
			kv->v = malloc(OBJSIZE);
			kv->n = NULL;
			puts(buf);
			if (json_typeof(objobj) == JSON_INTEGER)
			{
				printf("put key %s to key\n", node->by);
				kv->k = node->by;
				sprintf(kv->v, "%d", json_integer_value(objobj));
				printf("!!! %s: %s\n", kv->k, kv->v);
			}
			else if (json_typeof(objobj) == JSON_STRING)
			{
				printf("put key %s to key\n", node->by);
				kv->k = node->by;
				sprintf(kv->v, "%s", json_string_value(objobj));
				printf("!!! %s: %s\n", kv->k, kv->v);
			}

			kvflag = 1;
			puts("trying add");
			if (cur)
			{
				puts("add to next");
				for (; cur->n; cur=cur->n);
				cur->n = kv;
				printf("cur = %p, %p\n", cur, cur->n);
			}
			else
				root_kv = kv;
		}
		print_json_aux(arr_obj, buf, hash, root_kv, node, NULL);
		if ( kvflag )
		{
			if ( cur )
			{
				free(cur->n->v);
				free(cur->n);
				cur->n = NULL;
			}
			else
			{
				//free(root_kv->v);
				//free(root_kv);
				root_kv = NULL;
			}
		}
	}
	printf("bypass: %d\n", node->action);
	switch(node->action)
	{
		case JSON_PARSER_SUM:
			printf("bypass: sum\n");
			tommy_hashdyn_foreach_arg(node->hash, jsonparser_collector_foreach, node);
			//tommy_hashdyn_remove(node->hash, pjson_collector_compare, node->key, tommy_strhash_u32(0, node->key))
			break;
		case JSON_PARSER_AVG:
			printf("bypass: avg\n");
			tommy_hashdyn_foreach_arg(node->hash, jsonparser_collector_foreach, node);
			break;
		default:
			printf("bypass: by\n");
			break;
	}
}

json_t *load_json(const char *text) {
	json_t *root;
	json_error_t error;

	root = json_loads(text, 0, &error);

	if (root) {
		return root;
	} else {
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return (json_t *)0;
	}
}

#define MAX_CHARS 100000

void json_parse(char *line, tommy_hashdyn *hash)
{
	json_t *root = load_json(line);

	if (root) {
		/* print and release the JSON structure */
		print_json_aux(root, "uwsgi", hash, NULL, NULL, NULL);
		json_decref(root);
	}
}
int main(int argc, char **argv)
{
	char line[MAX_CHARS];

	printf("open jsonfile\n");
	FILE *fd = fopen(FILEE, "r");
	if (!fd)
		return -1;
	int rc = fread(line, 1, MAX_CHARS, fd);
	line[rc] = 0;

	tommy_hashdyn *hash = malloc(sizeof(*hash));
	tommy_hashdyn_init(hash);
	//tommy_hashdyn_foreach(hash, tommyhash_forearch);
	int i;
	for (i=1; i<argc; i++)
	{
		pjson_node *node = malloc(sizeof(*node));
		if (!strncmp(argv[i], "print::", 7) )
		{
			node->action = JSON_PARSER_PRINT;
			node->key = strndup(argv[i]+7, strcspn(argv[i]+7, "("));
			node->by = argv[i]+7 + strcspn(argv[i]+7, "(") +1;
			node->by[strcspn(node->by, ")")] = 0;
			node->hash = NULL;
		}
		else if (!strncmp(argv[i], "sum::", 5) )
		{
			printf("bypass: JSONPARSERSUM\n");
			node->action = JSON_PARSER_SUM;
			node->key = strndup(argv[i]+5, strcspn(argv[i]+5, "("));
			node->by = argv[i]+5 + strcspn(argv[i]+5, "(") +1;
			node->by[strcspn(node->by, ")")] = 0;
			node->hash = malloc(sizeof(tommy_hashdyn));
			tommy_hashdyn_init(node->hash);
		}
		else if (!strncmp(argv[i], "avg::", 5) )
		{
			printf("bypass: JSONPARSERAVG\n");
			node->action = JSON_PARSER_AVG;
			node->key = strndup(argv[i]+5, strcspn(argv[i]+5, "("));
			node->by = argv[i]+5 + strcspn(argv[i]+5, "(") +1;
			node->by[strcspn(node->by, ")")] = 0;
			node->hash = malloc(sizeof(tommy_hashdyn));
			tommy_hashdyn_init(node->hash);
		}
		else
		{
			printf("%s no action\n", argv[i]);
			free(node);
			continue;
		}
		tommy_hashdyn_insert(hash, &(node->node), node, tommy_strhash_u32(0, node->key));
	}
	fclose(fd);

	json_parse(line, hash);
	tommy_hashdyn_foreach(hash, jsonparser_free_foreach);
	tommy_hashdyn_done(hash);
	free(hash);
}
