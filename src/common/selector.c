#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <sys/stat.h>
#include "platform/platform.h"
#include "dstructures/metric.h"
#include "dstructures/rbtree.h"
#include "main.h"

size_t get_file_size(char *filename)
{
	struct stat st;
	stat(filename, &st);
	return st.st_size;
}

char* selector_getline( char *str, size_t str_n, char *fld, size_t fld_len, uint64_t num )
{
	uint64_t i, n, cu;
	for (i=0, n=0; i<str_n && n<num; i++, n++)
	{
		for (; i<str_n && str[i]!='\n'; i++ );
	}
	if ( i == str_n )
		return NULL;
	cu = i;
	for (; i<str_n && str[i]!='\n'; i++);

	// for detect overflow
	if ( fld_len < i-cu )
		return NULL;
	strlcpy(fld, str+cu, i-cu+1);
	return fld;
}

char* selector_get_field_by_str(char *str, size_t str_n, char *sub, int col, char *sep)
{
	uint64_t i;
	char *fld = malloc(str_n+1);
	for ( i=0; selector_getline(str, str_n, fld, str_n, i) ; i++ )
	{
		if ( strstr(fld, sub) )
		{
			if ( col != 0 )
			{
				int k;
				uint64_t frstchr, ndchr, crsr = 0, y;
				char *ssep = sep ? sep : "\r\n\t\0 " ;
				for ( k = 1; k<col; k++ )
				{
					crsr += strcspn(fld+crsr, ssep)+1;
				}
				frstchr = crsr;
				ndchr = strcspn(fld+crsr+frstchr, ssep) + frstchr;
				for ( y=0; y < ndchr; y++ )
				{
					fld[y]=fld[y+frstchr];
				}
				fld[y]='\0';
			}
			return fld;
		}
	}
	free(fld);
	return NULL;
}

char* selector_split_metric(char *text, size_t sz, char *nsep, size_t nsep_sz, char *sep, size_t sep_sz, char *prefix, size_t prefix_size, char **maps, size_t maps_size)
{
	int64_t i;
	int64_t j;
	int64_t n;
	char *pmetric = malloc(METRIC_SIZE);
	char *pfield = malloc(METRIC_SIZE);
	size_t cpy_sz;
	char *ret = malloc(METRIC_SIZE*maps_size);
	*ret = 0;
	int64_t count = 0;
	for (i=0; i<sz; i++)
	{
		char *cur = strstr(text+i, nsep);
		if (!cur)
			break;

		n = cur-text-i;
		size_t n_sz = METRIC_SIZE > n ? n : METRIC_SIZE;
		strlcpy(pfield, text+i, n_sz);
		i += n;

		cur = strstr(pfield, sep);
		if (!cur)
			continue;
		n = pfield+n-cur-2;

		cpy_sz = cur - pfield;
		if (metric_name_validator(pfield, cpy_sz))
			strlcpy(pmetric, pfield, cpy_sz+1);
		else
			continue;

		cur++;
		int rc = metric_value_validator(cur, n);
		if      (rc == ALLIGATOR_DATATYPE_INT)
		{
			int64_t pvalue = atoi(cur);
			metric_labels_add_auto_prefix(pmetric, prefix, &pvalue, rc, 0);
		}
		else if (rc == ALLIGATOR_DATATYPE_DOUBLE)
		{
			double pvalue = atof(cur);
			metric_labels_add_auto_prefix(pmetric, prefix, &pvalue, rc, 0);
		}
		else
		{
			for (j=0; j<maps_size; j++)
			{
				if(!strncmp(pmetric, maps[j], n_sz))
				{
					strncat(ret, pfield, n_sz);
					count++;
				}
			}
			continue;
		}
	}
	free(pfield);
	free(pmetric);
	if (count)
		return ret;
	else
	{
		free(ret);
		return NULL;
	}
}

void stlencat(stlen *str, char *str2, size_t len)
{
	//printf("CONCAT '%p'(SSIZE %zu) + '%s' (SIZE %zu)\n", str->s, strlen(str->s), str2, len);
	strncat(str->s, str2, len);
	str->l += len;
}

void stlentext(stlen *str, char *str2)
{
	size_t len = strlen(str2) +1;
	strlcpy(str->s, str2, len);
	str->l += len;
}

char *gettextfile(char *path, size_t *filesz)
{
	FILE *fd = fopen(path, "r");
	if (!fd)
		return NULL;

	fseek(fd, 0, SEEK_END);
	int64_t fdsize = ftell(fd)+1;
	rewind(fd);

#ifdef _WIN64
	int32_t psize = 10000;
#else
	int32_t psize = 0;
#endif

	char *buf = malloc(fdsize+psize);
	size_t rc = fread(buf, 1, fdsize, fd);
	rc++;
#ifndef _WIN64
	if (rc != fdsize)
	{
		fprintf(stderr, "I/O err read %s\n", path);
		return NULL;
	}
#endif
	buf[rc] = 0;
	if (filesz)
		*filesz = rc-1;
	fclose(fd);

	return buf;
}
