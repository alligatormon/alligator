#pragma once
#include "dstructures/tommy.h"
#include "events/context_arg.h"
#define SCHEDULER_DATASOURCE_INTERNAL 0
#define SCHEDULER_DATASOURCE_QUERY 1

typedef struct scheduler_node {
	char *name;
	char *action;
	char *lang;
	string *expr;
	char *datasource;
	uint8_t datasource_int;
	uv_timer_t *timer;
	uint64_t period;

	tommy_node node;
} scheduler_node;

scheduler_node* scheduler_get(char *name);
int scheduler_compare(const void* arg, const void* obj);
void scheduler_start(scheduler_node *sn);
void scheduler_stop(scheduler_node *sn);
void scheduler_push_json(json_t *scheduler);
void influxdb_parser_push();
void scheduler_del_json(json_t *scheduler);
void scheduler_del_all();
void scheduler_del(char *name);
