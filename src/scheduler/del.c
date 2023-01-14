#include "scheduler/type.h"
#include <stdlib.h>
#include "main.h"

void scheduler_object_del(scheduler_node *sn)
{
	if (!sn)
		return;

	if (sn->name)
		free(sn->name);
	if (sn->action)
		free(sn->action);
	if (sn->lang)
		free(sn->lang);
	if (sn->expr)
		string_free(sn->expr);
	if (sn->datasource)
		free(sn->datasource);

	scheduler_stop(sn);

	free(sn);
}

void scheduler_del_json(json_t *scheduler)
{
	json_t *jname = json_object_get(scheduler, "name");
	if (!jname)
		return;
	char *name = (char*)json_string_value(jname);

	scheduler_node *sn = alligator_ht_search(ac->scheduler, scheduler_compare, name, tommy_strhash_u32(0, name));
	alligator_ht_remove_existing(ac->scheduler, &(sn->node));
	scheduler_object_del(sn);
}

void scheduler_foreach_del_object(void *funcarg, void* arg)
{
	scheduler_node *sn = arg;

	scheduler_object_del(sn);
}

void scheduler_del_all()
{
	alligator_ht_foreach_arg(ac->scheduler, scheduler_foreach_del_object, NULL);
	alligator_ht_done(ac->scheduler);
	free(ac->scheduler);
}
