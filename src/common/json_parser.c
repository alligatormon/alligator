#include <stdio.h>
#include <stdlib.h>
#include <jansson.h>
#include <string.h>
#include "dstructures/tommyds/tommyds/tommy.h"
#include "dstructures/metric.h"

#define JSON_PARSER_PRINT 0
#define JSON_PARSER_SUM 1
#define JSON_PARSER_AVG 2
#define JSON_PARSER_LABEL 3
#define OBJSIZE 100
#define JSON_PARSER_SEPARATOR "_"

typedef struct pjson_collector
{
	double collector;
	char *key;
	char *label;
	int action;
	tommy_node node;
} pjson_collector;

typedef struct pjson_node
{
	char *key;
	char *by;
	char *exportname;
	char *replace;
	int action;
	size_t size;
	tommy_hashdyn *hash;
	tommy_node node;
} pjson_node;

typedef struct jsonparse_kv
{
	char *k;
	char *replace;
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
	if (node->action == JSON_PARSER_PRINT)
		return;

	if (node->action == JSON_PARSER_AVG)
		collector->collector /= node->size;

	printf("collector %d, %s=%lf\n", node->action, collector->key, collector->collector);
	puts(collector->key);
	//metric_labels_add_lbl(node->exportname, &collector->collector, ALLIGATOR_DATATYPE_DOUBLE, 0, "type", collector->key);
	printf("key %s(%zu)\n", collector->key, strlen(collector->key));
	metric_labels_add_lbl(node->key, &collector->collector, ALLIGATOR_DATATYPE_DOUBLE, 0, "key", collector->label);
	puts("done");
	//metric_labels_add_lbl(node->exportname, &collector->collector, ALLIGATOR_DATATYPE_DOUBLE, 0, "type", collector->key);

	//tommy_hashdyn_remove(node->hash, pjson_collector_compare, collector->key, tommy_strhash_u32(0, collector->key));
	//free(collector->key);
	//free(collector->label);
	//free(collector);
}

void print_json_aux(json_t *element, char *buf, tommy_hashdyn *hash, jsonparse_kv *kv, pjson_node *node, pjson_collector *collector);
void print_json_object(json_t *element, char *buf, tommy_hashdyn *hash, jsonparse_kv *kv, pjson_node *node);
void print_json_array(json_t *element, char *buf, tommy_hashdyn *hash, jsonparse_kv *kv);


void jsonparser_collector_free_foreach(void *funcarg, void* arg)
{
	pjson_collector *collector = arg;
	if (!collector)
		return;

	//tommy_hashdyn_remove(node->hash, pjson_collector_compare, collector->key, tommy_strhash_u32(0, collector->key));
	free(collector->key);
	free(collector->label);
	free(collector);
}
void jsonparser_free_foreach(void* arg)
{
	pjson_node *obj = arg;
	if ( obj->hash )
	{
		tommy_hashdyn_foreach_arg(obj->hash, jsonparser_collector_free_foreach, obj);
		tommy_hashdyn_done(obj->hash);
	}
	free(obj->hash);
	free(obj->key);
	free(obj->exportname);
	if (obj->replace)
		free(obj->replace);
	free(obj);
}

void print_json_aux(json_t *element, char *buf, tommy_hashdyn *hash, jsonparse_kv *kv, pjson_node *node, pjson_collector *collector)
{
	int jsontype = json_typeof(element);
	printf("DEBUG2: %s\n", buf);
	if (node)
		printf("NGG %p\n", node);
	jsonparse_kv *cur = kv;
	//switch (json_typeof(element)) {
	if (jsontype==JSON_OBJECT)
		print_json_object(element, buf, hash, kv, node);
	else if (jsontype==JSON_ARRAY)
		print_json_array(element, buf, hash, kv);
	else if (jsontype==JSON_STRING && !kv)
	{
		puts("SS: STRING1");
		printf("SF: finding %s, ", buf);
		pjson_node *pjnode = tommy_hashdyn_search(hash, pjson_compare, buf, tommy_strhash_u32(0, buf));
		printf("result: %p\n", pjnode);
		if (pjnode && pjnode->action == JSON_PARSER_LABEL)
		{
			int64_t num = 1;
			metric_labels_add_lbl(buf, &num, ALLIGATOR_DATATYPE_INT, 0, pjnode->by, json_string_value(element));
		}

		double af = atof(json_string_value(element));
		if ( !af )
			return;
	}
	else if (jsontype==JSON_STRING && kv)
	{
		puts("SS: STRING2");
		double af = atof(json_string_value(element));
		if ( !af )
			return;
		//for (; cur; cur = cur->n) printf("(%s:%s) ", cur->k, cur->v);

		if (cur && collector && collector->action == JSON_PARSER_PRINT)
		{
			alligator_labels* lblroot = malloc(sizeof(alligator_labels));
			alligator_labels* lbl = lblroot;
			for (; cur; cur = cur->n)
			{
				lbl->next = NULL;
				// for replace naming
				if (cur->replace)
					lbl->name = strdup(cur->replace);
				else
					lbl->name = strdup(cur->k);
				lbl->key = strdup(cur->v);

				printf("(%s:%s) ", cur->k, cur->v);
				if (cur->n)
				{
					lbl->next = malloc(sizeof(alligator_labels));
					lbl = lbl->next;
				}
				else
				{
					int64_t num = json_integer_value(element);
					metric_labels_add(strdup(buf), lblroot, &num, ALLIGATOR_DATATYPE_INT, 0);
				}
			}
		}
		else if (collector) collector->collector += json_integer_value(element);
	}
	else if (jsontype == JSON_INTEGER && !kv)
	{
		puts("SS: INTEGER1");
		int64_t num = json_integer_value(element);
		metric_labels_add_auto(buf, &num, ALLIGATOR_DATATYPE_INT, 0);
	}
	else if (jsontype == JSON_INTEGER && kv)
	{
		puts("SS: INTEGER2");
		if (cur && collector && collector->action == JSON_PARSER_PRINT)
		{
			alligator_labels* lblroot = malloc(sizeof(alligator_labels));
			alligator_labels* lbl = lblroot;
			for (; cur; cur = cur->n)
			{
				lbl->next = NULL;
				// for replace naming
				if (cur->replace)
					lbl->name = strdup(cur->replace);
				else
					lbl->name = strdup(cur->k);
				lbl->key = strdup(cur->v);

				printf("(%s:%s) ", cur->k, cur->v);
				if (cur->n)
				{
					lbl->next = malloc(sizeof(alligator_labels));
					lbl = lbl->next;
				}
				else
				{
					int64_t num = json_integer_value(element);
					metric_labels_add(strdup(buf), lblroot, &num, ALLIGATOR_DATATYPE_INT, 0);
				}
			}
		}
		else if (collector) collector->collector += json_integer_value(element);
	}
	else if (jsontype == JSON_REAL && !kv)
	{
		puts("SS: REAL1");
		double num = json_real_value(element);
		metric_labels_add_auto(buf, &num, ALLIGATOR_DATATYPE_DOUBLE, 0);
	}
	else if (jsontype == JSON_REAL && kv)
	{
		puts("SS: REAL2");
		double num = json_real_value(element);
		//for (; cur; cur = cur->n) printf("(%s:%s) ", cur->k, cur->v);
		if (cur && collector && collector->action == JSON_PARSER_PRINT)
		{
			alligator_labels* lblroot = malloc(sizeof(alligator_labels));
			alligator_labels* lbl = lblroot;
			for (; cur; cur = cur->n)
			{
				lbl->next = NULL;
				// for replace naming
				if (cur->replace)
					lbl->name = strdup(cur->replace);
				else
					lbl->name = strdup(cur->k);
				lbl->key = strdup(cur->v);

				printf("(%s:%s) ", cur->k, cur->v);
				if (cur->n)
				{
					lbl->next = malloc(sizeof(alligator_labels));
					lbl = lbl->next;
				}
				else
				{
					metric_labels_add(strdup(buf), lblroot, &num, ALLIGATOR_DATATYPE_DOUBLE, 0);
				}
			}
		}
		else if (collector) collector->collector += num;
	}
	else if (jsontype == JSON_TRUE && !kv)
	{
		puts("SS: TRUE1");
		int64_t num = 1;
		metric_labels_add_auto(buf, &num, ALLIGATOR_DATATYPE_INT, 0);
	}
	else if (jsontype == JSON_TRUE && kv)
	{
		puts("SS: TRUE2");
		//for (; cur; cur = cur->n) printf("(%s:%s) ", cur->k, cur->v);
		if (cur && collector && collector->action == JSON_PARSER_PRINT)
		{
			alligator_labels* lblroot = malloc(sizeof(alligator_labels));
			alligator_labels* lbl = lblroot;
			for (; cur; cur = cur->n)
			{
				lbl->next = NULL;
				// for replace naming
				if (cur->replace)
					lbl->name = strdup(cur->replace);
				else
					lbl->name = strdup(cur->k);
				lbl->key = strdup(cur->v);

				printf("(%s:%s) ", cur->k, cur->v);
				if (cur->n)
				{
					lbl->next = malloc(sizeof(alligator_labels));
					lbl = lbl->next;
				}
				else
				{
					int64_t num = 1;
					metric_labels_add(strdup(buf), lblroot, &num, ALLIGATOR_DATATYPE_INT, 0);
				}
			}
		}
		else if (collector) collector->collector += 1;
	}
	else if (jsontype == JSON_FALSE && !kv)
	{
		puts("SS: FALSE1");
		int64_t num = 0;
		metric_labels_add_auto(buf, &num, ALLIGATOR_DATATYPE_INT, 0);
	}
	else if (jsontype == JSON_FALSE && kv)
	{
		puts("SS: FALSE2");
		for (; cur; cur = cur->n) printf("(%s:%s) ", cur->k, cur->v);
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
		snprintf(skey, OBJSIZE, "%s%s%s", buf, JSON_PARSER_SEPARATOR, key);
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
				collector->key = strdup(colkey);
				collector->label = strdup(key);
				collector->action = node->action;
				puts("DEBUG3: start");
				printf("DEBUG3 %p %p %p %s\n", node->hash, collector, &(collector->node), collector->key);
				tommy_hashdyn_insert(node->hash, &(collector->node), collector, tommy_strhash_u32(0, collector->key));
				puts("DEBUG3: end");
			}
		}
		print_json_aux(value, skey, hash, kv, NULL, collector);
	}

}

void print_json_array(json_t *element, char *buf, tommy_hashdyn *hash, jsonparse_kv *root_kv)
{
	size_t i;
	size_t size = json_array_size(element);

	pjson_node *node = NULL;
	//printf("finding %s\n", buf);
	node = tommy_hashdyn_search(hash, pjson_compare, buf, tommy_strhash_u32(0, buf));
	//printf("result: %p\n", node);
	if (!node)
	{
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

			printf("\nffinding '%s'\n", node->by);
			json_t *objobj = json_object_get(arr_obj, node->by);
			if (!objobj)
			{
				return;
			}
			printf("result objobj %p\n", objobj);
			kv = malloc(sizeof(*kv));
			kv->v = malloc(OBJSIZE);
			kv->n = NULL;
			puts(buf);
			if (json_typeof(objobj) == JSON_INTEGER)
			{
				kv->k = node->by;
				kv->replace = node->replace;
				sprintf(kv->v, "%lld", json_integer_value(objobj));
			}
			else if (json_typeof(objobj) == JSON_STRING)
			{
				kv->k = node->by;
				kv->replace = node->replace;
				sprintf(kv->v, "%s", json_string_value(objobj));
			}

			kvflag = 1;
			if (cur)
			{
				for (; cur->n; cur=cur->n);
				cur->n = kv;
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
				free(root_kv->v);
				free(root_kv);
				root_kv = NULL;
			}
		}
	}
	switch(node->action)
	{
		case JSON_PARSER_SUM:
			//printf("bypass: sum\n");
			tommy_hashdyn_foreach_arg(node->hash, jsonparser_collector_foreach, node);
			//tommy_hashdyn_remove(node->hash, pjson_collector_compare, node->key, tommy_strhash_u32(0, node->key))
			break;
		case JSON_PARSER_AVG:
			//printf("bypass: avg\n");
			tommy_hashdyn_foreach_arg(node->hash, jsonparser_collector_foreach, node);
			break;
		default:
			printf("bypass: by %s:%s\n", node->by, buf);
			tommy_hashdyn_foreach_arg(node->hash, jsonparser_collector_foreach, node);
			break;
	}
	//puts("done done");
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

void json_parse(char *line, tommy_hashdyn *hash, char *name)
{
	json_t *root = load_json(line);
	if (!root)
	{
		printf("cannot parse json string:\n%s\n", line);
		return;
	}

	print_json_aux(root, name, hash, NULL, NULL, NULL);
	json_decref(root);
}
void json_parser_entry(char *line, int argc, char **argv, char *name)
{
	printf("string\n%s\n", line);

	tommy_hashdyn *hash = malloc(sizeof(*hash));
	tommy_hashdyn_init(hash);
	int i;
	for (i=0; i<argc; i++)
	{
		pjson_node *node = malloc(sizeof(*node));
		node->replace = NULL;
		if (!strncmp(argv[i], "print::", 7) )
		{
			node->action = JSON_PARSER_PRINT;
			node->exportname = strndup(argv[i]+7, strcspn(argv[i]+7, JSON_PARSER_SEPARATOR));
			node->key = strndup(argv[i]+7, strcspn(argv[i]+7, "("));
			node->by = argv[i]+7 + strcspn(argv[i]+7, "(") +1;
			size_t by_size = strcspn(node->by, ")");
			size_t slash_size = strcspn(node->by, "/");
			if(by_size > slash_size)
			{
				size_t replace_size = by_size-slash_size;
				node->replace = strndup(node->by+slash_size+1, replace_size);
				node->by[slash_size] = 0;
				node->replace[replace_size-1] = 0;
			}
			else
				node->by[by_size] = 0;
			//node->hash = NULL;
			node->hash = malloc(sizeof(tommy_hashdyn));
			tommy_hashdyn_init(node->hash);
			//printf("bypass: JSONPARSERPRINT %s\n", node->key);
		}
		else if (!strncmp(argv[i], "sum::", 5) )
		{
			//printf("bypass: JSONPARSERSUM\n");
			node->action = JSON_PARSER_SUM;
			node->exportname = strndup(argv[i]+5, strcspn(argv[i]+5, JSON_PARSER_SEPARATOR));
			node->key = strndup(argv[i]+5, strcspn(argv[i]+5, "("));
			node->by = argv[i]+5 + strcspn(argv[i]+5, "(") +1;
			node->by[strcspn(node->by, ")")] = 0;
			node->hash = malloc(sizeof(tommy_hashdyn));
			tommy_hashdyn_init(node->hash);
		}
		else if (!strncmp(argv[i], "avg::", 5) )
		{
			//printf("bypass: JSONPARSERAVG\n");
			node->action = JSON_PARSER_AVG;
			node->exportname = strndup(argv[i]+5, strcspn(argv[i]+5, JSON_PARSER_SEPARATOR));
			node->key = strndup(argv[i]+5, strcspn(argv[i]+5, "("));
			node->by = argv[i]+5 + strcspn(argv[i]+5, "(") +1;
			node->by[strcspn(node->by, ")")] = 0;
			node->hash = malloc(sizeof(tommy_hashdyn));
			tommy_hashdyn_init(node->hash);
		}
		else if (!strncmp(argv[i], "label::", 7) )
		{
			node->action = JSON_PARSER_LABEL;
			node->exportname = strndup(argv[i]+7, strcspn(argv[i]+7, JSON_PARSER_SEPARATOR));
			node->key = strndup(argv[i]+7, strcspn(argv[i]+7, "("));
			node->by = argv[i]+7 + strcspn(argv[i]+7, "(") +1;
			node->by[strcspn(node->by, ")")] = 0;
			node->hash = malloc(sizeof(tommy_hashdyn));
			printf("bypass: JSONPARSERLABEL exportname='%s', key='%s', by='%s'\n", node->exportname, node->key, node->by);
			tommy_hashdyn_init(node->hash);
		}
		else
		{
			//printf("bypass: %s no action\n", argv[i]);
			free(node);
			continue;
		}
		tommy_hashdyn_insert(hash, &(node->node), node, tommy_strhash_u32(0, node->key));
	}
	//fclose(fd);

	json_parse(line, hash, name);
	tommy_hashdyn_foreach(hash, jsonparser_free_foreach);
	tommy_hashdyn_done(hash);
	free(hash);
}
