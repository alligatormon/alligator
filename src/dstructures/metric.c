#include <stdio.h>
#include "rbtree.h"
#include "metric.h"
#include "main.h"
#include "dstructures/tommy.h"

void metricprint_forearch(void *funcarg, void* arg)
{
	kv_metric *metric = arg;
	if (!metric)
		return;
	stlen *str = ((stlen*)funcarg);
	char buf[NAMELEN];
	if ( metric->tree )
	{
		snprintf(buf,NAMELEN,"%s {", metric->key);
		rb_tree_labels_for(metric->tree->root, buf, 0, str);
	}
	else
	{
		if ( !metric->key )
			return;
		if ( metric->res->en == ALLIGATOR_DATATYPE_DOUBLE )
			snprintf(buf,NAMELEN,"%s %lf\n", metric->key, metric->res->d);
		else if ( metric->res->en == ALLIGATOR_DATATYPE_INT )
			snprintf(buf,NAMELEN,"%s %"d64"\n", metric->key, metric->res->i);
		else if ( metric->res->en == ALLIGATOR_DATATYPE_STRING )
			snprintf(buf,NAMELEN,"%s %s\n", metric->key, metric->res->s);
		//write(STDOUT_FILENO, buf, strlen(buf));
		//write(s, buf, strlen(buf));
		strncat(str->s, buf, MAX_RESPONSE_SIZE);
	}
}

int ns_node_compare(const void* arg, const void* obj)
{
        char *s1 = (char*)arg;
        char *s2 = ((namespace_struct*)obj)->key;
        return strcmp(s1, s2);
}

void labels_erase(alligator_labels *lbl)
{
	while (lbl)
	{
		alligator_labels *tmp = lbl;
		lbl = lbl->next;
		free(tmp->name);
		free(tmp->key);
		free(tmp);
	}
}

void metric_labels_add(char *name, alligator_labels *lbl, void* value, int datatype, char *ns_use)
{
	if ( !value && !name )
	{
		labels_erase(lbl);
		return;
	}
	if ( !value )
	{
		labels_erase(lbl);
		free(name);
		return;
	}

	int64_t i;
	for(i=0; name[i]; i++)
	{
		if(name[i] == ' ' || name[i] == '-' || name[i] == '.')
			name[i] = '_';
	}

	if (datatype == ALLIGATOR_DATATYPE_DOUBLE && isnan(*(double*)value))
		*(double*)value = 0;

	if (datatype == ALLIGATOR_DATATYPE_DOUBLE && isinf(*(double*)value))
		*(double*)value = 0;

	extern aconf* ac;

	// selecting ns
	namespace_struct *ns = ac->nsdefault;
	if ( ns_use )
	{
		ns = tommy_hashdyn_search(ac->_namespace, ns_node_compare, ns_use, tommy_strhash_u32(0, ns_use));
		if ( !ns )
		{
			ns = malloc(sizeof(*ns));
			ns->key = ns_use;
			tommy_hashdyn_insert(ac->_namespace, &ns->node, ns, tommy_strhash_u32(0, ns->key));
		}
		else
		{
			free(ns_use);
		}
	}


	// selecting metric kv name
	kv_metric *metric = tommy_hashdyn_search(ns->metric, ns_node_compare, name, tommy_strhash_u32(0, name));
	if ( !metric )
	{
		metric = calloc(1, sizeof(*metric));
		metric->key = name;
		tommy_hashdyn_insert(ns->metric, &metric->node, metric, tommy_strhash_u32(0, metric->key));
	}
	else
	{
		free(name);
	}
	if (!metric)
	{
		labels_erase(lbl);
		return;
	}


	// selecting labels
	rb_node *res = NULL;
	if ( !lbl )
	{
		if ( !(metric->res) )
			metric->res = malloc(sizeof(*res));
		res = metric->res;
	}
	else
	{
		alligator_labels *lblroot = lbl;
		// TODO: check 
		if ( metric && metric->tree ) { }
		else
		{
			metric->tree = calloc(1, sizeof(rb_tree));
		}
		rb_tree *tree = metric->tree;
		while (tree && lbl)
		{
			res = rb_find(tree,lbl->key);
			if (!res)
			{
				res = rb_insert(tree,lbl);
				res->en = 0;
				res->stree = calloc(sizeof(rb_tree),1);
				lbl = lbl->next;
			}
			else
			{
				lblroot = lbl;
				lbl = lbl->next;
				free(lblroot->name);
				free(lblroot->key);
				free(lblroot);
			}
			tree = res->stree;
		}
	}


	switch(datatype)
	{
		case ALLIGATOR_DATATYPE_STRING:
			res->s = (char*)value;
			break;
		case ALLIGATOR_DATATYPE_INT:
			res->i = *(int64_t*)value;
			break;
		case ALLIGATOR_DATATYPE_DOUBLE:
			res->d = *(double*)value;
			break;
	}

	res->timestamp = setrtime();
	//filenode_push(res);
	res->en = datatype;
}

void metric_labels_print(stlen *str)
{
	extern aconf *ac;

	tommy_hashdyn_foreach_arg(ac->nsdefault->metric, metricprint_forearch, str);

	//char buf[NAMELEN];
	//rb_tree_labels_for(tree->root, buf, 0, fd);
}

void metric_labels_add_auto(char *mname, void* value, int datatype, char *ns_use)
{
	char *name = strdup(mname);
	metric_labels_add(name, NULL, value, datatype, 0);
}

void metric_labels_add_auto_prefix(char *mname, char *prefix, void* value, int datatype, char *ns_use)
{
	char *name = malloc(strlen(mname)+strlen(prefix)+1);
	name[0] = 0;
	strcat(name, prefix);
	strcat(name, mname);
	metric_labels_add(name, NULL, value, datatype, 0);
}

void metric_labels_add_lbl(char *mname, void* value, int datatype, char *ns_use, char *lblname1, const char *lblkey1)
{
	char *name = strdup(mname);
	alligator_labels* lbl = malloc(sizeof(alligator_labels));
	lbl->next = NULL;
	lbl->name = strdup(lblname1);
	lbl->key = strdup(lblkey1);
	metric_labels_add(name, lbl, value, datatype, 0);
}

void metric_labels_add_lbl2(char *mname, void* value, int datatype, char *ns_use, char *lblname1, char *lblkey1, char *lblname2, const char *lblkey2)
{
	char *name = strdup(mname);
	alligator_labels* lbl = malloc(sizeof(alligator_labels));
	lbl->next = malloc(sizeof(alligator_labels));
	lbl->name = strdup(lblname1);
	lbl->key = strdup(lblkey1);
	alligator_labels *tmp = lbl->next;
	tmp->next = NULL;
	tmp->name = strdup(lblname2);
	tmp->key = strdup(lblkey2);
	metric_labels_add(name, lbl, value, datatype, 0);
}

void metric_labels_add_lbl3(char *mname, void* value, int datatype, char *ns_use, char *lblname1, char *lblkey1, char *lblname2, const char *lblkey2, char *lblname3, const char *lblkey3)
{
	char *name = strdup(mname);
	alligator_labels* lbl = malloc(sizeof(alligator_labels));
	lbl->next = malloc(sizeof(alligator_labels));
	lbl->name = strdup(lblname1);
	lbl->key = strdup(lblkey1);

	alligator_labels *tmp = lbl->next;
	tmp->next = malloc(sizeof(alligator_labels));;
	tmp->name = strdup(lblname2);
	tmp->key = strdup(lblkey2);

	tmp = tmp->next;
	tmp->next = NULL;
	tmp->name = strdup(lblname3);
	tmp->key = strdup(lblkey3);
	metric_labels_add(name, lbl, value, datatype, 0);
}

void metric_labels_add_lbl4(char *mname, void* value, int datatype, char *ns_use, char *lblname1, char *lblkey1, char *lblname2, const char *lblkey2, char *lblname3, const char *lblkey3, char *lblname4, const char *lblkey4)
{
	char *name = strdup(mname);
	alligator_labels* lbl = malloc(sizeof(alligator_labels));
	alligator_labels *tmp = lbl;
	tmp->next = malloc(sizeof(alligator_labels));
	tmp->name = strdup(lblname1);
	tmp->key = strdup(lblkey1);

	tmp = tmp->next;
	tmp->next = malloc(sizeof(alligator_labels));;
	tmp->name = strdup(lblname2);
	tmp->key = strdup(lblkey2);

	tmp = tmp->next;
	tmp->next = malloc(sizeof(alligator_labels));;
	tmp->name = strdup(lblname3);
	tmp->key = strdup(lblkey3);

	tmp = tmp->next;
	tmp->name = strdup(lblname4);
	tmp->key = strdup(lblkey4);

	tmp->next = NULL;
	metric_labels_add(name, lbl, value, datatype, 0);
}
