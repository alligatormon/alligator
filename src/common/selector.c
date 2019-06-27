#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <sys/stat.h>
#include "platform/platform.h"
#include "main.h"
#include "metric/namespace.h"

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
	char *ret = 0;
	if (maps_size)
	{
		ret = malloc(METRIC_SIZE*maps_size);
		*ret = 0;
	}
	int64_t count = 0;
	for (i=0; i<sz; i++)
	{
		char *cur = strstr(text+i, nsep);
		if (!cur)
			break;
		//cur += nsep_sz - 1;

		n = cur-text-i;
		size_t n_sz = METRIC_SIZE > n ? n : METRIC_SIZE;
		strlcpy(pfield, text+i, n_sz+1);
		i += n + nsep_sz - 1;

		cur = strstr(pfield, sep);
		if (!cur)
			continue;
		//n = pfield+n-cur-2;
		n = pfield+n_sz-cur-sep_sz;

		cpy_sz = cur - pfield;
		if (metric_name_validator(pfield, cpy_sz))
			strlcpy(pmetric, pfield, cpy_sz+1);
		else
			continue;

		cur += sep_sz;
		//cur++;
		int rc = metric_value_validator(cur, n);
		if      (rc == DATATYPE_INT)
		{
			int64_t pvalue = atoll(cur);

			int64_t fullsize = prefix_size+cpy_sz+1;
			char fullmetric[fullsize];
			strncpy(fullmetric, prefix, prefix_size);
			strlcpy(fullmetric+prefix_size, pmetric, fullsize-prefix_size);
			metric_add_auto(fullmetric, &pvalue, rc, 0);
		}
		else if (rc == DATATYPE_DOUBLE)
		{
			double pvalue = atof(cur);

			int64_t fullsize = prefix_size+cpy_sz+1;
			char fullmetric[fullsize];
			strncpy(fullmetric, prefix, prefix_size);
			strlcpy(fullmetric+prefix_size, pmetric, fullsize-prefix_size);
			metric_add_auto(fullmetric, &pvalue, rc, 0);
			//metric_add_auto_prefix(pmetric, prefix, &pvalue, rc, 0);
		}
		else
		{
			for (j=0; j<maps_size; j++)
			{
				if(!strncmp(pmetric, maps[j], n_sz))
				{
					strncat(ret, pfield, n_sz);
					strncat(ret, "\n", 1);
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

int64_t int_get_next(char *buf, size_t sz, char sep, int64_t *cursor)
{
	for (; *cursor<sz; ++(*cursor))
	{
		for (; *cursor<sz && buf[*cursor]==sep; ++(*cursor));
		if (isdigit(buf[*cursor]) || buf[*cursor] == '-')
		{
			int64_t ret = atoll(buf+(*cursor));
			for (; *cursor<sz && (isdigit(buf[*cursor]) || buf[*cursor] == '-'); ++(*cursor));
			++(*cursor);

			return ret;
		}
	}
	return 0;
}

int64_t getkvfile(char *file)
{
	char temp[20];
	FILE *fd = fopen(file, "r");
	if (!fd)
		return 0;

	if ( !fgets(temp, 20, fd) )
	{
		fclose(fd);
		return 0;
	}

	fclose(fd);

	return atoll(temp);
}

string* string_init(size_t max)
{
	string *ret = malloc(sizeof(*ret));
	ret->m = max;
	ret->s = malloc(max);
	ret->l = 0;

	return ret;
}
string* string_init_str(char *str, size_t max)
{
	string *ret = malloc(sizeof(*ret));
	ret->m = max;
	ret->s = str;
	ret->l = 0;

	return ret;
}

void string_cat(string *str, char *strcat, size_t len)
{
	size_t str_len = str->l;
	//printf("1212str_len=%zu\n", str_len);
	if(str_len+len >= str->m)
		return;

	size_t copy_size = len < (str->m - str_len) ? len : str_len;
	//printf("string '%s'\nstr_len=%zu\nstrcat='%s'\ncopy_size=%zu\nlen=%zu\n", str->s, str_len, strcat, copy_size+1, len);
	memcpy(str->s+str_len, strcat, copy_size);
	str->l += copy_size;
}

void string_uint(string *str, uint64_t u)
{
	size_t str_len = str->l;
	if(str_len+20 >= str->m)
		return;

	char num[20];
	snprintf(num, 20, "%"u64, u);

	size_t len = strlen(num);
	size_t copy_size = len < (str->m - str_len) ? len : str_len;
	strlcpy(str->s+str_len, num, copy_size+1);
	str->l += copy_size;
}

void string_int(string *str, int64_t i)
{
	size_t str_len = str->l;
	if(str_len+20 >= str->m)
		return;

	char num[20];
	snprintf(num, 20, "%"d64, i);

	size_t len = strlen(num);
	size_t copy_size = len < (str->m - str_len) ? len : str_len;
	strlcpy(str->s+str_len, num, copy_size+1);
	str->l += copy_size;
}

void string_double(string *str, double d)
{
	size_t str_len = str->l;
	if(str_len+20 >= str->m)
		return;

	char num[20];
	snprintf(num, 20, "%lf", d);

	size_t len = strlen(num);
	size_t copy_size = len < (str->m - str_len) ? len : str_len;
	strlcpy(str->s+str_len, num, copy_size+1);
	str->l += copy_size;
}
