#include "query/type.h"
#include "common/logs.h"
#include "main.h"
extern aconf *ac;

int query_push(json_t *query) {
	json_t *jmake = json_object_get(query, "make");
	if (!jmake) {
		glog(L_INFO, "query_push: not specified 'make' option\n");
		return 0;
	}
	char *make = (char*)json_string_value(jmake);

	json_t *jexpr = json_object_get(query, "expr");
	if (!jexpr) {
		glog(L_INFO, "query_push: not specified 'expr' option in '%s'\n", make);
		return 0;
	}
	char *expr = (char*)json_string_value(jexpr);

	json_t *jaction = json_object_get(query, "action");
	char *action = (char*)json_string_value(jaction);

	json_t *jdatasource = json_object_get(query, "datasource");
	if (!jdatasource) {
		glog(L_INFO, "query_push: not specified 'datasource' option in '%s'\n", make);
		return 0;
	}
	char *datasource = (char*)json_string_value(jdatasource);

	json_t *jns = json_object_get(query, "ns");
	char *ns = (char*)json_string_value(jns);

	json_t *jfield = json_object_get(query, "field");

	query_node *qn = calloc(1, sizeof(*qn));

	alligator_ht *qf_hash = query_get_field(jfield);

	if (expr)
		qn->expr = strdup(expr);
	if (action)
		qn->action = strdup(action);
	if (ns)
		qn->ns = strdup(ns);
	qn->qf_hash = qf_hash;

	qn->make = strdup(make);
	qn->datasource = strdup(datasource); // part of query ds

	query_ds *qds = alligator_ht_search(ac->query, queryds_compare, qn->datasource, tommy_strhash_u32(0, qn->datasource));
	if (!qds)
	{
		qds = calloc(1, sizeof(*qds));
		qds->hash = calloc(1, sizeof(alligator_ht));
		alligator_ht_init(qds->hash);
		qds->datasource = qn->datasource;
		alligator_ht_insert(ac->query, &(qds->node), qds, tommy_strhash_u32(0, qds->datasource));
	}
	if (ac->log_level > 0)
		printf("create query node make %p: '%s', expr '%s', ns '%s'\n", qn, qn->make, qn->expr, qn->ns);
	alligator_ht_insert(qds->hash, &(qn->node), qn, tommy_strhash_u32(0, qn->make));

	return 1;
}
