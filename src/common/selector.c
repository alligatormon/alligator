#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <sys/stat.h>
#include "platform/platform.h"
#include "dstructures/metric.h"
#include "dstructures/rbtree.h"

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

void selector_get_plain_metrics(char *m, size_t ms, char *sep, char *msep, char *prefix, size_t prefix_size)
{
	//printf("use selector %p (%zu), sep: %s, msep: %s prefix: %s (%zu)\n", m, ms, sep, msep, prefix, prefix_size);
	char *pmetric = malloc ( 1001 );
	double pvalue;
	char *ssep = sep ? sep : "\n\r\0";
	uint64_t crsr = 0;
	uint64_t crsre = 0;

	crsr = 0;

	while ( ( crsre += strcspn(m+crsr, ssep)+1 ) <= ms )
	{
		char *mssep = msep ? msep : ":";
		int mval_sym;
		strlcpy(pmetric, m+crsr, crsre-crsr);
		mval_sym = strcspn ( pmetric, mssep );
		pvalue = atof(pmetric+mval_sym+1);
		pmetric[mval_sym]='\0';
		
		//as_add(as, prefix, prefix_size, pvalue, pmetric);
		//alligator_labels *labels = NULL;
		//as_labels_push(&labels, "key", 3, "val", 3);
		//as_ladd(as, prefix, prefix_size, pvalue, pmetric, NULL);
		//printf ("metric_labels_add(%s, %p, %lld, %d, %d)\n", pmetric, NULL, &pvalue, ALLIGATOR_DATATYPE_INT, 0);
		int64_t i;
		for (i=0; i<mval_sym; i++)
			if ( pmetric[i] == '.' )
				pmetric[i] = '_';
		metric_labels_add_auto_prefix(pmetric, prefix, &pvalue, ALLIGATOR_DATATYPE_DOUBLE, 0);
		//metric_labels_add(pmetric, NULL, &pvalue, ALLIGATOR_DATATYPE_INT, 0);

		crsr = crsre;
	}

	free(pmetric);
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
