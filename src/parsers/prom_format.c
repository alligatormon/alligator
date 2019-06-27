#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include "main.h"

typedef struct string_ring {
	char *s;
	struct string_ring *next;
} string_ring;

tommy_hashdyn* labels_parse(char *lblstr, size_t l, char *get, string_ring **sr_cache)
{
	uint64_t i;
	//alligator_labels *lbl = NULL;
	//alligator_labels *lblroot = NULL;
	tommy_hashdyn *lbl = malloc(sizeof(*lbl));
	tommy_hashdyn_init(lbl);
	char name[METRIC_SIZE];
	//char *mname;
	//char *key;

	uint64_t index_get;
	uint64_t prev_get;
	char mname[255];
	char key[255];
	string_ring *sr = *sr_cache = calloc(1, sizeof(*sr));
	printf("alloc %p\n", sr);
	//string_ring *sr_cur = sr;

	// for pushgateway GET labels parser
	if (get)
	{
		char *metrics_str  = strstr(get, "/metrics/");
		if (metrics_str)
		{
			//string_ring *sr = calloc(1, sizeof(*sr));
			string_ring *sr_cur = 0, *sr = 0;
			puts("???????????");
			for (index_get = metrics_str - get + 9; ; ++index_get)
			{
				puts("!!!!!!!!!");
				for (; get[index_get]=='/' && get[index_get]!=0; ++index_get);
				prev_get = index_get;
				for (; get[index_get]!='/' && get[index_get]!=0; ++index_get);
				strlcpy(mname, get+prev_get, index_get-prev_get+1);

				for (; get[index_get]=='/' && get[index_get]!=0; ++index_get);
				prev_get = index_get;
				for (; get[index_get]!='/' && get[index_get]!=0; ++index_get);
				strlcpy(key, get+prev_get, index_get-prev_get+1);

				printf("insert '%s' with key '%s'\n", mname, key);
				if (!sr)
				{
					sr_cur = sr = *sr_cache = malloc(sizeof(*sr));
					printf("alloc %p->%p\n", sr_cur);
				}
				else
				{
					sr_cur->next = malloc(sizeof(*sr_cur));
					sr_cur = sr_cur->next;
					printf("alloc %p->%p\n", sr_cur);
				}
				char *dup_name = sr_cur->s = strdup(mname);

				sr_cur->next = malloc(sizeof(*sr_cur));
				sr_cur = sr_cur->next;
				printf("alloc %p\n", sr_cur);

				char *dup_key = sr_cur->s = strdup(key);
				sr_cur->next = 0;

				labels_hash_insert(lbl, dup_name, dup_key);

				if (get[index_get]==0)
					break;
			}

		}
	}

	for ( i=0; i<l; i++ )
	{
		uint64_t cur = i;
		i = strcspn(lblstr, "{");
		if ( i == l )
			return lbl;
		strlcpy(name,lblstr+cur,i-cur);
		break;
	}
	i++;
	printf("name: '%s'\n", name);
	int64_t endlabels = strcspn(lblstr, "}");
	for ( ; i<endlabels; i++ )
	{
		uint64_t cur = i;
		if ( (lblstr[i] == ' ') || (lblstr[i] == '\t') || (lblstr[i] == ',') )
			continue;
		i = strcspn(lblstr+cur, "=")+cur;
		//////mname = malloc(i-cur+1);
		//printf("malloc(%llu)\n", i-cur+1);
		//mal += i-cur+1;
		strlcpy(mname,lblstr+cur,i-cur+1);
		i+=2;
		cur = i;
		i = strcspn(lblstr+cur, "\"") + cur;
		////key = malloc(i-cur+1);
		strlcpy(key,lblstr+cur,i-cur+1);

		labels_hash_insert(lbl, mname, key);
		//printf("name: '%s', key: '%s'\n", lbl->name, lbl->key);
	}

	return lbl;
}

void metric_labels_parse(char *metric, size_t l, char *get)
{
	//printf("metric %s with len %zu\n", metric, l);
	uint64_t i;
	char name[255];
	size_t name_len = 0;
	double value = 0;
	for ( i=0; i<l; i++ )
	{
		puts("11111111");
		uint64_t cur = i;
		if ( (metric[i] == ' ') || (metric[i] == '\t') )
			continue;
		i = strcspn(metric, "{");
		printf("%d == %d\n", i, l);
		if ( i == l )
		{
			i = strcspn(metric+cur, " \t");
			printf("trying if %d == %d\n", (i+cur), l);
			if ( (i+cur) == l )
			{
				//free(name);
				puts("> FREE NAME");
				return;
			}
			//strlcpy(name,metric+cur,i);
		}
		name_len = i-cur;
		strlcpy(name,metric+cur,name_len+1);

			printf("trying if '%c' '%c' with '#'\n", name[0], name[1]);
			if (name[0] == '#')
			{
				//free(name);
				puts("> FREE NAME");
				return;
			}

		printf("METRIC NAME is %s (%d)\n", name, i-cur+1);
		break;
	}
	i++;


	//printf("TESTDIGIT symbol(%c) from text(%s) with num %d\n", metric[i], metric, i);
	tommy_hashdyn *lbl;
	string_ring *sr;
	if ( isdigit(metric[i]) )
	{
		//puts("DIGIT");
		value = atof(metric+i);

		lbl = labels_parse(metric, l, get, &sr);
		//metric_add_auto(name, &value, DATATYPE_DOUBLE, 0);
	//	return;
	}
	else
	{

		int64_t endlabels = strcspn(metric, "}") +1;

		lbl = labels_parse(metric, l, get, &sr);
		printf("lbl pointer %p\n", lbl);

		i = strcspn(metric+i, "}")+i;
		for ( ; i<l; i++ )
		{
			if ( (metric[i] == ' ') || (metric[i] == '\t') )
				continue;
		}
		value = atof(metric+endlabels);
	}

	metric_name_normalizer(name, name_len);
	metric_add(name, lbl, &value, DATATYPE_DOUBLE, 0);

	// free caches
	string_ring *sr_cur = sr;
	while (sr_cur)
	{
		printf("free %p\n", sr);
		sr_cur = sr_cur->next;
		free(sr->s);
		free(sr);
		sr = sr_cur;
	}
}

int char_fgets(char *str, char *buf, int64_t *cnt, size_t len)
{
	if (*cnt >= len)
	{
		strncpy(buf, "\0", 1);
		return 0;
	}
	int64_t i = strcspn(str+(*cnt), "\n");
	//if ( i == len-(*cnt) )
	//{
	//	printf("%"d64" == %zu - %"d64"\n", i, len, *cnt);
	//	strncpy(buf, "\0", 1);
	//	return 0;
	//}
	strlcpy(buf, str+(*cnt), i+1);
	i++;
	*cnt += i;
	return i;
}

void prom_getmetrics_n (char *buf, size_t len, char *get)
{
	//printf("prom getmetrics '%s'\n", buf);
	char tmp[65535];
	int64_t i;
	int64_t cnt = 0;
		
	while ( (i=char_fgets(buf, tmp, &cnt, strlen(buf))) )
	{
		//printf("tmp = '%s' (%"d64")\n", tmp, i);
		metric_labels_parse(tmp, strlen(tmp), get);
	}
}
