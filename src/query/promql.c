#include "query/promql.h"
#include "common/selector.h"
tommy_hashdyn* promql_parser(tommy_hashdyn* lbl, char *query, size_t size, char *name, int *func, string *groupkey)
{
	//printf("==== parse %s\n", query);
	if (!size)
		return lbl;

	if (!query)
		return lbl;

	if (!lbl)
	{
		lbl = malloc(sizeof(*lbl));
		tommy_hashdyn_init(lbl);
	}

	*func = QUERY_FUNC_COUNT;

	uint64_t i;
	uint64_t cur;
	uint64_t funcur;
	char key[255];
	char value[255];
	uint64_t new;
	char *str = query;
	str += strspn(str, " \t\r\n");
	//printf("2str %p\n", str);
	cur = strcspn(str, "{");
	funcur = strcspn(str, "( \t");

	// find function
	uint64_t jump = 0;
	//printf("cur %"u64" > funcur %"u64"\n", cur, funcur);
	if (cur > funcur)
	{
		if (!strncasecmp(str, "count", 5))
		{
			*func = QUERY_FUNC_COUNT;
			jump = 5;
		}
		else if (!strncasecmp(str, "sum", 3))
		{
			*func = QUERY_FUNC_SUM;
			jump = 3;
		}
		else if (!strncasecmp(str, "absent", 6))
		{
			*func = QUERY_FUNC_ABSENT;
			jump = 6;
		}
		else if (!strncasecmp(str, "present", 7))
		{
			*func = QUERY_FUNC_PRESENT;
			jump = 7;
		}
		else if (!strncasecmp(str, "min", 3))
		{
			*func = QUERY_FUNC_MIN;
			jump = 3;
		}
		else if (!strncasecmp(str, "max", 3))
		{
			*func = QUERY_FUNC_MAX;
			jump = 3;
		}
		else if (!strncasecmp(str, "avg", 3))
		{
			*func = QUERY_FUNC_AVG;
			jump = 3;
		}
	}

	// find metric name
	//printf("jump %"u64"\n", jump);
	str += jump;
	str += strspn(str, " \t\r\n()");

	// skip group by
	if (!strncmp(str, "by", 2))
	{
		i = strcspn(str, "(");
		str += i + 1;
		i = strcspn(str, ")");
		uint64_t k;
		for (uint64_t j = 0; j < i; j++)
		{
			k = strspn(str, ", \t\r\n");
			str += k;
			k += k;
			if (*str == ')')
				break;

			//printf("\t(%d/%d) cur position: '%s'\n", j, i, str);
			k = strcspn(str, ",)");
			string_cat(groupkey, str, k);
			string_cat(groupkey, ",", 1);
			str += k;
			j += k;
		}
		groupkey->s[groupkey->l - 1] = 0;
		printf("group_key '%s'\n", groupkey->s);

		str += strspn(str, " \t\r\n()");
	}

	//printf("3str %p\n", str);
	cur = strcspn(str, "{) \t");
	for (i = 0; i < cur; i++)
	{
		name[i] = str[i];
	}
	name[i] = 0;
	//printf("name is %s, %d\n", name, *func);

	int check_open = strcspn(str+cur, "{");
	//printf("{ sym: %d(%s) '%c'\n", check_open, str+cur, str[cur+check_open]);
	if (str[cur+check_open] == '{')
	{
		cur += strspn(str+cur, "{ \t");
		size = size - (str - query);
		int size_to_end = strcspn(str+cur, "}");
		for (; cur <= size_to_end;)
		{
			//printf("parsing (%d/%d) '%s'\n", cur, size_to_end, str+cur);
			new = strcspn(str+cur, " \t=\"");
			if (new)
			{
				strlcpy(key, str+cur, new+1);

				cur += new;
				cur += strspn(str+cur, " \t=\"");

				new = strcspn(str+cur, " \"");
				if (new)
				{
					strlcpy(value, str+cur, new+1);
					cur += new;

					//printf("key: %s, value: %s\n", key, value);
					labels_hash_insert_nocache(lbl, key, value);
				}
			}

			cur++;
			if (cur <= size_to_end)
			{
				cur += strcspn(str+cur, ",");
				cur += strspn(str+cur, ", \t");
			}
		}
	}

	if (!groupkey->l)
	{
		str = strstr(query, "by");
		if (str)
		{
			i = strcspn(str, "(");
			str += i + 1;
			i = strcspn(str, ")");
			uint64_t k;
			for (uint64_t j = 0; j < i; j++)
			{
				k = strspn(str, ", \t\r\n");
				str += k;
				k += k;
				if (*str == ')')
					break;

				//printf("\t(%d/%d) cur position: '%s'\n", j, i, str);
				k = strcspn(str, ",)");
				string_cat(groupkey, str, k);
				string_cat(groupkey, ",", 1);
				str += k;
				j += k;
			}
			groupkey->s[groupkey->l - 1] = 0;
			//printf("group_key '%s'\n", groupkey->s);

			str += strspn(str, " \t\r\n()");
		}
	}

	return lbl;
}
