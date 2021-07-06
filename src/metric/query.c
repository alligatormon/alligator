#include <stdint.h>
#include <metric/query.h>
#include "metric/namespace.h"
#include "query/promql.h"
extern aconf *ac;

void metric_str_build_query (char *namespace, string *str, char *name, tommy_hashdyn *hash, int func, string *groupkey, int serializer, char delimiter)
{
	namespace_struct *ns = get_namespace(namespace);

	if (name && !*name)
		name = NULL;
	//printf("namespace %s ns %p, name %s\n", namespace, ns, name);

	size_t labels_count = tommy_hashdyn_count(hash);
	metric_tree *tree = ns->metrictree;
	labels_t *labels_list = labels_initiate(hash, name, 0, 0, 0);

	serializer_context *sc = serializer_init(serializer, str, delimiter);

	if (tree && tree->root)
	{
		metrictree_serialize_query(tree->root, labels_list, groupkey, sc, labels_count);
		serializer_do(sc, str);
	}

	serializer_free(sc);
}

string* metric_query_deserialize(size_t init_size, metric_query_context *mqc, int serializer, char delimiter, char *namespace)
{
	string *body = string_init(init_size);
	metric_str_build_query(namespace, body, mqc->name, mqc->lbl, mqc->func, mqc->groupkey, serializer, delimiter);
	return body;
}
