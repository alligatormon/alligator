void parse_query(char *query)
{
	if (!query)
		return;

	uint64_t numsym = strncmp(query, "sumbycountavgminmax");

	char *str = query + numsym;
	if (!strncmp(str, "sum"))
		printf("Aggregate is sum\n");
	else if (!strncmp(str, "count"))
		printf("Aggregate is count\n");
	else if (!strncmp(str, "avg"))
		printf("Aggregate is avg\n");
	else if (!strncmp(str, "min"))
		printf("Aggregate is min\n");
	else if (!strncmp(str, "max"))
		printf("Aggregate is max\n");
	else if (!strncmp(str, ""))
		printf("Aggregate by\n");
}
