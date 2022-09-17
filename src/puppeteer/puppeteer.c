#include <jansson.h>
#include "main.h"
#include "puppeteer/puppeteer.h"

int puppeteer_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((puppeteer_node*)obj)->url->s;
	return strcmp(s1, s2);
}

puppeteer_node* puppeteer_get(const char *name)
{
	puppeteer_node *pn = alligator_ht_search(ac->puppeteer, puppeteer_compare, name, tommy_strhash_u32(0, name));
	if (pn)
		return pn;
	else
		return NULL;
}

void puppeteer_insert_object(const char *url, char *value)
{
	puppeteer_node *pn = calloc(1, sizeof(*pn));
	pn->url = string_init_dup((char*)url);
	pn->value = value;

	if (ac->log_level > 0)
		printf("puppeteer insert url '%s'\n", pn->url->s);

	alligator_ht_insert(ac->puppeteer, &(pn->node), pn, tommy_strhash_u32(0, pn->url->s));
}

void puppeteer_delete_object(puppeteer_node *pn)
{
	if (!pn)
		return;

	if (alligator_ht_remove_existing(ac->puppeteer, &(pn->node)))
	{
		if (pn->url)
			string_free(pn->url);
		if (pn->value)
			free(pn->value);
		free(pn);
	}
}

void puppeteer_insert(json_t *root)
{
	const char *key;
	json_t *value;
	json_object_foreach(root, key, value)
	{
		puppeteer_node *pn = puppeteer_get(key);
		if (!pn)
		{
			char *str_value = json_dumps(value, 0);
			puppeteer_insert_object(key, str_value);
		}
	}
}

void puppeteer_delete(json_t *root)
{
	const char *key;
	json_t *value;
	json_object_foreach(root, key, value)
	{
		puppeteer_node *pn = puppeteer_get(key);
		puppeteer_delete_object(pn);
	}
}

void puppeteer_foreach_crawl(void *funcarg, void* arg)
{
	puppeteer_node *pn = arg;
	//printf("free url '%s'\n", pn->url->s);
	string_free(pn->url);
	free(pn);
}

void puppeteer_foreach_run(void *funcarg, void* arg)
{
	//string *domains = funcarg;
	puppeteer_node *pn = arg;

	//string_cat(domains, " ", 1);
	//string_string_cat(domains, pn->url);

	json_t *puppeteer_conf = funcarg;

	json_error_t error;
	json_t *arg_val = json_loads(pn->value, 0, &error);
	if (!arg_val)
	{
		fprintf(stderr, "puppeteer json error on line %d: %s\n", error.line, error.text);
		return;
	}

	//printf("insert %s:%s\n", pn->url->s, pn->value);
	json_array_object_insert(puppeteer_conf, pn->url->s, arg_val);

}
void puppeteer_crawl(uv_timer_t* handle) {
	if (!alligator_ht_count(ac->puppeteer))
		return;

	string *domains = string_new();

	json_t *puppeteer_conf = json_object();
	alligator_ht_foreach_arg(ac->puppeteer, puppeteer_foreach_run, puppeteer_conf);
	char *get_data = json_dumps(puppeteer_conf, 0);
	json_decref(puppeteer_conf);
	string_cat(domains, get_data, strlen(get_data));

	//printf("string is %s\n", domains->s);

	char *expr = malloc(domains->l + 128);
	size_t expr_len = snprintf(expr, domains->l + 127, "exec:///bin/node /var/lib/alligator/puppeteer-alligator.js '%s'", domains->s);
	string *work_dir = string_init_dup("/var/lib/alligator");
	context_arg *carg = aggregator_oneshot(NULL, expr, expr_len, NULL, 0, NULL, "NULL", NULL, NULL, 0, NULL, NULL, 0, work_dir);
	if (carg)
	{
		carg->no_metric = 1;
		carg->no_exit_status = 1;
	}

	string_free(domains);
}

void puppeteer_generator()
{
	uv_timer_init(ac->loop, &ac->puppeteer_timer);
	uv_timer_start(&ac->puppeteer_timer, puppeteer_crawl, ac->aggregator_startup, ac->aggregator_repeat);
}

void puppeteer_foreach_done(void *funcarg, void* arg)
{
	puppeteer_node *pn = arg;
	//printf("free url '%s'\n", pn->url->s);
	string_free(pn->url);
	free(pn);
}

void puppeteer_done()
{
	alligator_ht_foreach_arg(ac->puppeteer, puppeteer_foreach_done, NULL);
	alligator_ht_done(ac->puppeteer);
	free(ac->puppeteer);
}
