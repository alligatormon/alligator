#include "scheduler/type.h"
#include <string.h>
#include "common/selector.h"
#include "common/units.h"
#include "common/logs.h"
#include "main.h"

void scheduler_push_json(json_t *scheduler)
{
	json_t *jname = json_object_get(scheduler, "name");
	if (!jname)
	{
		glog(L_ERROR, "no defined 'name' for scheduler\n");
		return;
	}
	char *name = (char*)json_string_value(jname);

	json_t *jaction = json_object_get(scheduler, "action");
	char *action = (char*)json_string_value(jaction);

	json_t *jlang = json_object_get(scheduler, "lang");
	char *lang = (char*)json_string_value(jlang);

	json_t *jdatasource = json_object_get(scheduler, "datasource");
	char *datasource = (char*)json_string_value(jdatasource);

	json_t *jexpr = json_object_get(scheduler, "expr");
	char *expr = (char*)json_string_value(jexpr);

	scheduler_node* sn = scheduler_get(name);
	if (sn)
	{
		glog(L_ERROR, "Scheduler with name %s was already create\n", name);
		return;
	}

	sn = calloc(1, sizeof(*sn));
	sn->name = strdup(json_string_value(jname));

	json_t *jperiod = json_object_get(scheduler, "period");
	sn->period = 10000;
	if (json_typeof(jperiod) == JSON_STRING)
		sn->period = get_ms_from_human_range(json_string_value(jperiod), json_string_length(jperiod));
	else
		sn->period = json_integer_value(jperiod);

	if (action)
		sn->action = strdup(action);

	if (lang)
		sn->lang = strdup(lang);

	if (!action && !lang)
		glog(L_ERROR, "Scheduler without 'action' or 'lang' fields: %s\n", name);

	if (datasource)
	{
		sn->datasource = strdup(datasource);
		if (strcmp(datasource, "internal"))
			sn->datasource_int = SCHEDULER_DATASOURCE_QUERY;
	}

	if (expr)
		sn->expr = string_init_dupn(expr, json_string_length(jexpr));

	uint32_t hash = tommy_strhash_u32(0, sn->name);
	alligator_ht_insert(ac->scheduler, &(sn->node), sn, hash);
	scheduler_start(sn);

	glog(L_ERROR, "Scheduler with name %s created\n", name);
}
