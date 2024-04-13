#pragma once
#include "dstructures/tommy.h"
#include "events/context_arg.h"
//#define SERIALIZATOR_OPENMETRICS 0
//#define SERIALIZATOR_GRAPHITE 1
//#define SERIALIZATOR_CARBON2 2
//#define SERIALIZATOR_INFLUXDB 3
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
int scheduler_compare(const void *a1, const void *a2);
void scheduler_start(scheduler_node *sn);
void scheduler_stop(scheduler_node *sn);
void scheduler_push_json(json_t *scheduler);
void influxdb_parser_push();
void scheduler_del_json(json_t *scheduler);
void scheduler_del_all();
