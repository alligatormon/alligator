#pragma once
#include <oniguruma.h>
#include "dstructures/tommy.h"
#include "events/context_arg.h"
#include <jansson.h>

typedef struct grok_pattern_node {
	char name[1024];
	char regex[65535];
} grok_pattern_node;

typedef struct grok_pattern {
	grok_pattern_node *nodes;
	size_t count;
} grok_pattern;

typedef struct grok_node
{
	char *name;
	char *value;
	string *match;
	string *expanded_match;
	regex_t *reg;
	alligator_ht *labels;

	tommy_node node;
} grok_node;

typedef struct grok_ds
{
	char *key;
	uint8_t pattern_applied;
	alligator_ht *hash;
	tommy_node node;
} grok_ds;

grok_ds* grok_get(char *key);
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
