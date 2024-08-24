#include "query/type.h"
#include "common/logs.h"
#include "main.h"
extern aconf *ac;

void query_field_clean_foreach(void *funcarg, void* arg)
{
	query_field *qf = arg;
	free(qf->field);
}

void query_node_del(query_node *qn)
{
	if (qn->expr)
		free(qn->expr);
	if (qn->make)
		free(qn->make);
	if (qn->action)
		free(qn->action);
	if (qn->ns)
		free(qn->ns);
	if (qn->datasource)
		free(qn->datasource);
	if (qn->action)
		free(qn->action);
	if (qn->labels)
		labels_hash_free(qn->labels);

	if (qn->qf_hash)
	{
		alligator_ht_foreach_arg(qn->qf_hash, query_field_clean_foreach, NULL);
		alligator_ht_done(qn->qf_hash);
		free(qn->qf_hash);
	}
	free(qn);
}

void query_node_foreach_done(void *funcarg, void* arg)
{
	query_node *qn = arg;
	query_node_del(qn);
}

int query_del(json_t *query)
{
	json_t *jmake = json_object_get(query, "make");
	if (!jmake) {
		glog(L_INFO, "query_del: not specified 'make' option\n");
		return 0;
	}
	char *make = (char*)json_string_value(jmake);

	json_t *jdatasource = json_object_get(query, "datasource");
	if (!jdatasource) {
		glog(L_INFO, "query_del: not specified 'datasource' option in '%s'\n", make);
		return 0;
	}
	char *datasource = (char*)json_string_value(jdatasource);

	query_ds *qds = alligator_ht_search(ac->query, queryds_compare, datasource, tommy_strhash_u32(0, datasource));
	if (qds)
	{
		query_node *qn = alligator_ht_search(qds->hash, query_compare, make, tommy_strhash_u32(0, make));
		if (qn)
		{
			alligator_ht_remove_existing(qds->hash, &(qn->node));

			query_node_del(qn);
		}

		uint64_t count = alligator_ht_count(qds->hash);
		if (!count)
		{
			alligator_ht_done(qds->hash);
			free(qds->hash);
			free(qds->datasource);
			free(qds);
		}
	}

	return 1;
}

void query_foreach_done(void *funcarg, void* arg)
{
	query_ds *qds = arg;

	alligator_ht_foreach_arg(qds->hash, query_node_foreach_done, NULL);
	alligator_ht_done(qds->hash);
	free(qds->hash);

	//alligator_ht_foreach_arg(qn->qf_hash, query_hash_foreach_done, NULL);
	//alligator_ht_done(qn->qf_hash);
	//free(qn->qf_hash);

	//free(qn);
	free(qds);
}

void query_stop()
{
	alligator_ht_foreach_arg(ac->query, query_foreach_done, NULL);
	alligator_ht_done(ac->query);
	free(ac->query);
}
