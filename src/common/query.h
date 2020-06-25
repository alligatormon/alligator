typedef struct query_node
{
	char *query;
	char *make;
	char *action;

	struct query_node *next;
} query_node;

typedef struct query_list
{
	query_node *head;
	query_node *tail;
} query_list;
