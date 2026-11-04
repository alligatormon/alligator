#pragma once
#include <pcre.h>
#include <pthread.h>
#include "dstructures/tommy.h"
#include "events/context_arg.h"
#include <jansson.h>

typedef struct grok_multimetric_node {
	char *key;
	string *metric_name;

	tommy_node node;
} grok_multimetric_node;

typedef struct grok_pattern_node {
	char name[1024];
	char regex[65535];
} grok_pattern_node;

typedef struct grok_pattern {
	grok_pattern_node *nodes;
	size_t count;
	struct grok_pattern *retired_next; /* kept until grok_stop after reload */
} grok_pattern;

typedef struct grok_node
{
	char *name;
	char *value;
	string *match;
	string *expanded_match;
	pcre *reg;
	pcre_extra *pcre_extra;
	int capturecount;
	int namecount;
	int name_entry_size;
	const char *nametable;
	alligator_ht *labels;
	uint8_t compile_failed;
	/* Primary series: 0 = counter (metric_update), 1 = gauge last-value (metric_add). */
	uint8_t is_gauge;

	tommy_node node;
} grok_node;

typedef struct grok_ds
{
	char *key;
	/* Accessed with __atomic_* (GCC 4.8 / CentOS 7 — no <stdatomic.h>).
	 * want_epoch bumped on push/reload; have_epoch set after expand/compile. */
	uint32_t want_epoch;
	uint32_t have_epoch;
	uint32_t mm_epoch; /* bumped when gps->mm / multimetrics built; carg refresh */
	uint8_t zombie; /* removed from ac->grok; destroy when refs hit 0 */
	uint8_t destroy_claimed; /* 0/1 via __atomic_exchange_n — single destroy owner */
	pthread_rwlock_t lock;
	int refs;
	mapping_metric *mm;
	string *separator;
	alligator_ht *hash;
	alligator_ht *gmm_quantile;
	alligator_ht *gmm_bucket;
	alligator_ht *gmm_le;
	alligator_ht *gmm_counter;
	alligator_ht *gmm_gauge;

	alligator_ht *gmm_splited_quantile;
	alligator_ht *gmm_splited_bucket;
	alligator_ht *gmm_splited_le;
	alligator_ht *gmm_splited_counter;
	alligator_ht *gmm_splited_tags;
	alligator_ht *gmm_splited_inherit_tag;
	tommy_node node;
} grok_ds;

grok_ds* grok_get(char *key);
void grok_put(grok_ds *gps);
void grok_life_lock(void);
void grok_life_unlock(void);
alligator_ht* grok_get_field(json_t *jfield);
grok_node *grok_get_node(grok_ds *gps, char *name);
void grok_set_values(grok_node *gn);
int grok_del(json_t *grok);
int grok_push(json_t *grok);
void grok_stop();
void grok_handler(char *metrics, size_t size, context_arg *carg);
int grok_compare(const void* arg, const void* obj);
int grokds_compare(const void* arg, const void* obj);
int grok_patterns_init();
int grok_multimetric_hash_compare(const void* arg, const void* obj);
void grok_expand(string *src, string **dst, grok_pattern *patterns);
int grok_expand_ok(string *src, string **dst, grok_pattern *patterns);
void grok_sanitize_capture_name(const char *in, char *out, size_t outsz);
void grok_ds_free_multimetrics(grok_ds *gps);
void grok_node_del(grok_node *gn);
void grok_ds_destroy(grok_ds *gps);
void grok_parse_multimetrics(grok_ds *gps, json_t *grok);
void grok_patterns_free_retired(void);
