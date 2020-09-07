typedef struct query_node
{
	char *expr;
	char *make;
	char *action;
	char *field;
	char *datasource;

	struct query_node *next;
} query_node;

typedef struct query_list
{
	query_node *head;
	query_node *tail;
} query_list;
