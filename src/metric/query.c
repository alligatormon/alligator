#include <stdint.h>
void parse_query(char *query)
{
	if (!query)
		return;

	uint64_t numsym = strcmp(query, "sumbycountavgminmax");

	char *str = query + numsym;
	if (!strcmp(str, "sum"))
		printf("Aggregate is sum\n");
	else if (!strcmp(str, "count"))
		printf("Aggregate is count\n");
	else if (!strcmp(str, "avg"))
		printf("Aggregate is avg\n");
	else if (!strcmp(str, "min"))
		printf("Aggregate is min\n");
	else if (!strcmp(str, "max"))
		printf("Aggregate is max\n");
	else if (!strcmp(str, ""))
		printf("Aggregate by\n");
}
