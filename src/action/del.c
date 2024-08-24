#include "main.h"
#include "dstructures/ht.h"
#include "action/type.h"
#include "common/logs.h"

void action_del(json_t *action)
{
	json_t *jname = json_object_get(action, "name");
	if (!jname)
	{
		glog(L_ERROR, "delete action failed, 'name' is empty\n");
		return;
	}
	char *name = (char*)json_string_value(jname);

	action_node *an = alligator_ht_search(ac->action, action_compare, name, tommy_strhash_u32(0, name));
	if (an)
	{
		alligator_ht_remove_existing(ac->action, &(an->node));

		if (an->expr)
			free(an->expr);
		if (an->name)
			free(an->name);
		if (an->ns)
			free(an->ns);
		if (an->af_hash)
			free(an->af_hash);
		if (an->work_dir)
			string_free(an->work_dir);
		if (an->engine)
			string_free(an->engine);
		if (an->index_template)
			string_free(an->index_template);
		if (an->parser_name)
			free(an->parser_name);
		//if (an->datasource)
		//	free(an->datasource);
		free(an);
	}

	uint64_t count = alligator_ht_count(ac->action);
	if (!count)
	{
		alligator_ht_done(ac->action);
	}
}
