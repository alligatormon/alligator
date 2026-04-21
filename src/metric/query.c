#include <stdint.h>
#include <metric/query.h>
#include "metric/labels.h"
#include "metric/namespace.h"
#include "query/promql.h"
#include "common/logs.h"
extern aconf *ac;

void metric_str_build_query (char *namespace, string *str, char *name, alligator_ht *hash, int func, string *groupkey, int serializer, char delimiter, string_tokens **multistring, string *engine, string *index_template, void *an)
{
	if (!ac)
		return;

	namespace_struct *ns = get_namespace(namespace);
	if (!ns || !ns->metrictree)
		return;

	if (name && !*name)
		name = NULL;

	/* labels_initiate consumes and frees the hash; dup so promql mqc->lbl stays valid if reused. */
	alligator_ht *hash_work = hash ? labels_dup(hash) : alligator_ht_init(NULL);
	size_t labels_count = alligator_ht_count(hash_work);
	metric_tree *tree = ns->metrictree;
	glog(L_DEBUG, "metric_str_build_query: ns='%s' name='%s' func=%d labels=%zu groupby='%s' serializer=%d\n", namespace ? namespace : "", name ? name : "", func, labels_count, (groupkey && groupkey->s) ? groupkey->s : "", serializer);

	serializer_context *sc = serializer_init(serializer, str, delimiter, engine, index_template, an);
	if (!sc)
	{
		if (multistring)
			*multistring = NULL;
		labels_hash_free(hash_work);
		return;
	}

	if (tree && tree->root && tree->rwlock)
	{
		labels_t *labels_list = labels_initiate(ns, hash_work, name, 0, 0, 0);
		metrictree_serialize_query(tree, labels_list, groupkey, sc, labels_count);
		labels_head_free(labels_list);
		serializer_do(sc, str);
		glog(L_DEBUG, "metric_str_build_query: serialized_len=%zu\n", str ? str->l : 0);
	}
	else
		labels_hash_free(hash_work);

	if (multistring)
		*multistring = sc->multistring;

	serializer_free(sc);
}

string* metric_query_deserialize(size_t init_size, metric_query_context *mqc, int serializer, char delimiter, char *namespace, string_tokens **multistring, string *engine, string *index_template, void *an)
{
	string *body = string_init(init_size);
	if (!mqc)
		return body;
	metric_str_build_query(namespace, body, mqc->name, mqc->lbl, mqc->func, mqc->groupkey, serializer, delimiter, multistring, engine, index_template, an);

	return body;
}
