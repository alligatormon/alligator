#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include "main.h"
#include "dstructures/rbtree.h"
#define GETMETRIC 1000

alligator_labels* labels_parse(char *lblstr, size_t l)
{
	uint64_t i;
	alligator_labels *lbl = NULL;
	alligator_labels *lblroot = NULL;
	char name[GETMETRIC];
	for ( i=0; i<l; i++ )
	{
		uint64_t cur = i;
		i = strcspn(lblstr, "{");
		if ( i == l )
			return NULL;
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
		if ( lbl == NULL )
		{
			lblroot = malloc(sizeof(alligator_labels));
			//printf("malloc(%zu)\n", sizeof(alligator_labels));
			//mal += sizeof(alligator_labels);
			lbl = lblroot;
		}
		else
		{
			lbl->next = malloc(sizeof(alligator_labels));
			lbl->next->xss = NULL;
			//printf("malloc(%zu)\n", sizeof(alligator_labels));
			//mal += sizeof(alligator_labels);
			lbl = lbl->next;
		}
		lbl->next = NULL;
		i = strcspn(lblstr+cur, "=")+cur;
		lbl->name = malloc(i-cur+1);
		//printf("malloc(%llu)\n", i-cur+1);
		//mal += i-cur+1;
		strlcpy(lbl->name,lblstr+cur,i-cur+1);
		i+=2;
		cur = i;
		i = strcspn(lblstr+cur, "\"") + cur;
		lbl->key = malloc(i-cur+1);
		strlcpy(lbl->key,lblstr+cur,i-cur+1);
		//printf("name: '%s', key: '%s'\n", lbl->name, lbl->key);
	}
	return lblroot;
}

void metric_labels_parse(char *metric, size_t l)
{
	printf("metric %s with len %zu\n", metric, l);
	uint64_t i;
	char *name = malloc(GETMETRIC);
	double value = 0;
	alligator_labels *lbl = NULL;
	alligator_labels *lblroot = NULL;
	for ( i=0; i<l; i++ )
	{
		uint64_t cur = i;
		if ( (metric[i] == ' ') || (metric[i] == '\t') )
			continue;
		i = strcspn(metric, "{");
		if ( i == l )
		{
			i = strcspn(metric+cur, " \t");
			if ( (i+cur) == l )
			{
				free(name);
				return;
			}
			strlcpy(name,metric+cur,i);
		}
		strlcpy(name,metric+cur,i-cur+1);
		break;
	}
	i++;


	//printf("TESTDIGIT symbol(%c) from text(%s) with num %d\n", metric[i], metric, i);
	if ( isdigit(metric[i]) )
	{
		//puts("DIGIT");
		value = atof(metric+i);
		metric_labels_add(name, NULL, &value, ALLIGATOR_DATATYPE_DOUBLE, 0);
		return;
	}

	int64_t endlabels = strcspn(metric, "}") +1;

	lblroot = labels_parse(metric, l);

	i = strcspn(metric+i, "}")+i;
	for ( ; i<l; i++ )
	{
		if ( (metric[i] == ' ') || (metric[i] == '\t') )
			continue;
	}
	value = atof(metric+endlabels);

	lbl = lblroot;
	if ( !lbl )
	{
		free(name);
		return;
	}
	while ( lbl->next )
	{
		//printf("\tlabel: '%s', key: '%s'\n", lbl->name, lbl->key);

		lbl = lbl->next;
	}
	metric_labels_add(name, lblroot, &value, ALLIGATOR_DATATYPE_DOUBLE, 0);
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

void prom_getmetrics_n (char *buf, size_t len)
{
	//printf("prom getmetrics '%s'\n", buf);
	char tmp[65535];
	int64_t i;
	int64_t cnt = 0;
	while ( (i=char_fgets(buf, tmp, &cnt, strlen(buf))) )
	{
		//printf("tmp = '%s' (%"d64")\n", tmp, i);
		metric_labels_parse(tmp, strlen(tmp));
	}
}
