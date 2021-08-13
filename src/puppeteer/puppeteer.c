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

void puppeteer_insert_object(const char *url)
{
	puppeteer_node *pn = calloc(1, sizeof(*pn));
	pn->url = string_init_dup((char*)url);

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
			puppeteer_insert_object(key);
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
	string *domains = funcarg;
	puppeteer_node *pn = arg;

	string_cat(domains, " ", 1);
	string_string_cat(domains, pn->url);
}
void puppeteer_crawl(uv_timer_t* handle) {
	string *domains = string_new();

	if (!alligator_ht_count(ac->puppeteer))
		return;

	alligator_ht_foreach_arg(ac->puppeteer, puppeteer_foreach_run, domains);

	//printf("string is %s\n", domains->s);

	char *expr = malloc(1024);
	size_t expr_len = snprintf(expr, 1024, "exec:///bin/node /var/lib/alligator/puppeteer.js %s", domains->s);
	aggregator_oneshot(NULL, expr, expr_len, NULL, 0, NULL, "NULL", NULL, NULL, 0, NULL, NULL, 0);

	string_free(domains);
}

void puppeteer_generator()
{
	uv_timer_t *timer1 = calloc(1, sizeof(*timer1));
	uv_timer_init(ac->loop, timer1);
	uv_timer_start(timer1, puppeteer_crawl, ac->aggregator_startup, ac->aggregator_repeat);
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
