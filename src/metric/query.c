#include <stdint.h>
#include <metric/query.h>
#include "metric/namespace.h"
#include "query/promql.h"
extern aconf *ac;

void metric_str_build_query (char *namespace, string *str, char *name, alligator_ht *hash, int func, string *groupkey, int serializer, char delimiter, string_tokens **multistring, string *engine, string *index_template)
{
	namespace_struct *ns = get_namespace(namespace);

	if (name && !*name)
		name = NULL;

	size_t labels_count = alligator_ht_count(hash);
	metric_tree *tree = ns->metrictree;

	serializer_context *sc = serializer_init(serializer, str, delimiter, engine, index_template);

	if (tree && tree->root)
	{
		labels_t *labels_list = labels_initiate(ns, hash, name, 0, 0, 0);
		metrictree_serialize_query(tree, labels_list, groupkey, sc, labels_count);
		labels_head_free(labels_list);
		serializer_do(sc, str);
	}

	if (multistring)
		*multistring = sc->multistring;

	serializer_free(sc);
}

string* metric_query_deserialize(size_t init_size, metric_query_context *mqc, int serializer, char delimiter, char *namespace, string_tokens **multistring, string *engine, string *index_template)
{
	string *body = string_init(init_size);
	metric_str_build_query(namespace, body, mqc->name, mqc->lbl, mqc->func, mqc->groupkey, serializer, delimiter, multistring, engine, index_template);

	return body;
}
