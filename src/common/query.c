#include "common/query.h"
#include "stdlib.h"

void query_processing()
{
	
}

query_node* query_push(query_list *ql, char *query, char *make, char *action)
{
	query_node *qn;

	if (!ql)
		return NULL;

	if (!ql->tail)
		qn = ql->head = ql->tail = calloc(1, sizeof(query_node));
	else
	{
		qn = ql->tail->next = calloc(1, sizeof(query_node));
		ql->tail = qn;
	}

	qn->query = query;
	qn->make = make;
	qn->action = action;

	return qn;
}
