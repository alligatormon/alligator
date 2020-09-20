#include <string.h>
#include <pcre.h>
#include "dstructures/tommy.h"
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <sys/stat.h>
#include "platform/platform.h"
#include "main.h"
#include "metric/namespace.h"

#define PLAIN_METRIC_SIZE 1000

size_t get_file_size(char *filename)
{
	struct stat st;
	stat(filename, &st);
	return st.st_size;
}

size_t get_any_file_size(char *filename)
{
	FILE *fd = fopen(filename, "r");
	if (!fd)
		return 0;

	fseek(fd, 0, SEEK_END);
	size_t fdsize = ftell(fd)+1;
	fclose(fd);
	return fdsize;
}

uint64_t get_file_atime(char *filename)
{
	struct stat st;
	stat(filename, &st);
	return st.st_atime;
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

char* selector_split_metric(char *text, size_t sz, char *nsep, size_t nsep_sz, char *sep, size_t sep_sz, char *prefix, size_t prefix_size, char **maps, size_t maps_size, context_arg *carg)
{
	int64_t i;
	int64_t j;
	int64_t n;
	char *pmetric = malloc(METRIC_SIZE);
	char *pfield = malloc(METRIC_SIZE);
	size_t cpy_sz;
	char *ret = 0;
	if (maps && maps_size)
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
			metric_add_auto(fullmetric, &pvalue, rc, carg);
		}
		else if (rc == DATATYPE_DOUBLE)
		{
			double pvalue = atof(cur);

			int64_t fullsize = prefix_size+cpy_sz+1;
			char fullmetric[fullsize];
			strncpy(fullmetric, prefix, prefix_size);
			strlcpy(fullmetric+prefix_size, pmetric, fullsize-prefix_size);
			metric_add_auto(fullmetric, &pvalue, rc, carg);
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
	*ret->s = 0;
	ret->l = 0;
	//printf("alloc: %p\n", ret->s);

	return ret;
}

void string_new_size(string *str, size_t len)
{
	uint64_t newsize = str->m*2+len;
	char *newstr_char = malloc(newsize);
	memcpy(newstr_char, str->s, str->l+1);
	char *oldstr_char = str->s;
	str->s = newstr_char;
	str->m = newsize;

	free(oldstr_char);
}

void string_null(string *str)
{
	*str->s = 0;
	str->l = 0;
}

void string_free(string *str)
{
	//printf("free: %p\n", str->s);
	free(str->s);
	free(str);
}

string* string_init_str(char *str, size_t max)
{
	string *ret = malloc(sizeof(*ret));
	ret->m = max;
	ret->s = str;
	*ret->s = 0;
	ret->l = 0;

	return ret;
}

string* string_init_add(char *str, size_t len, size_t max)
{
	string *ret = malloc(sizeof(*ret));
	ret->m = max;
	ret->s = str;
	ret->l = len;

	return ret;
}


string* string_init_add_auto(char *str)
{
	string *ret = malloc(sizeof(*ret));
	ret->m = ret->l = strlen(str);
	ret->s = str;

	return ret;
}

void string_cat(string *str, char *strcat, size_t len)
{
	size_t str_len = str->l;
	//printf("1212str_len=%zu\n", str_len);
	if(str_len+len >= str->m)
		string_new_size(str, len);

	size_t copy_size = len < (str->m - str_len) ? len : str_len;
	//printf("string '%s'\nstr_len=%zu\nstrcat='%s'\ncopy_size=%zu\nlen=%zu\n", str->s, str_len, strcat, copy_size+1, len);
	memcpy(str->s+str_len, strcat, copy_size);
	str->l += copy_size;
	str->s[str->l] = 0;
}

void string_string_cat(string *str, string *src)
{
	size_t str_len = str->l;
	size_t src_len = src->l;
	if(str_len+src_len >= str->m)
		string_new_size(str, src_len);

	memcpy(str->s+str_len, src->s, src_len);
	str->l += src_len;
	str->s[str->l] = 0;
}

void string_merge(string *str, string *src)
{
	string_string_cat(str, src);
	string_free(src);
}

void string_uint(string *str, uint64_t u)
{
	size_t str_len = str->l;
	if(str_len+20 >= str->m)
		string_new_size(str, 20);

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
		string_new_size(str, 20);

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
		string_new_size(str, 20);

	char num[20];
	snprintf(num, 20, "%lf", d);

	size_t len = strlen(num);
	size_t copy_size = len < (str->m - str_len) ? len : str_len;
	strlcpy(str->s+str_len, num, copy_size+1);
	str->l += copy_size;
}

string* string_init_alloc(char *str, size_t max)
{
	string *ret = malloc(sizeof(*ret));
	ret->m = max;
	ret->s = malloc(max);
	memcpy(ret->s, str, max);
	ret->l = max;

	return ret;
}

int match_mapper_compare(const void* arg, const void* obj)
{
        char *s1 = (char*)arg;
        char *s2 = ((match_string*)obj)->s;
        return strcmp(s1, s2);
}

int8_t match_mapper(match_rules *mrules, char *str, size_t size, char *name)
{
	int64_t n = tommy_hashdyn_count(mrules->hash);
	if ((!n) && (!mrules->head))
	{
		return 1;
	}

	match_string *ms = tommy_hashdyn_search(mrules->hash, match_mapper_compare, str, tommy_strhash_u32(0, str));
	if (ms)
	{
		++ms->count;
		return 1;
	}

	regex_list *node = mrules->head;
	while (node)
	{
		int str_vector[1];
		int pcreExecRet = pcre_jit_exec(node->re_compiled, node->pcre_extra, str, size, 0, 0, str_vector, 1, node->jstack);

		if (pcreExecRet < 0)
			node = node->next;
		else
		{
			++node->count;
			return 1;
		}
	}

	return 0;
}

void match_push(match_rules *mrules, char *str, size_t len)
{
	if(*str == '/')
	{	// chained regex
		char strnormalized[len-2];
		strlcpy(strnormalized, str+1, len-1);
		const char *pcreErrorStr;
		int pcreErrorOffset;
		regex_list *node = malloc(sizeof(regex_list));;
		node->next = NULL;
		node->name = strdup(strnormalized);
		metric_name_normalizer(node->name, strlen(node->name));
		node->jstack = pcre_jit_stack_alloc(1000,10000);
		node->re_compiled = pcre_compile(strnormalized, 0, &pcreErrorStr, &pcreErrorOffset, NULL);
		if(!node->re_compiled)
		{
			printf("ERROR: Could not compile '%s': %s\n", strnormalized, pcreErrorStr);
			free(node);
			return;
		}

		node->pcre_extra = pcre_study(node->re_compiled, PCRE_STUDY_JIT_COMPILE, &pcreErrorStr);
		if(pcreErrorStr)
		{
			printf("ERROR: Could not study '%s': %s\n", strnormalized, pcreErrorStr);
			free(node);
			return;
		}

		if (mrules->tail && mrules->head)
		{
			mrules->tail->next = node;
			mrules->tail = node;
		}
		else
		{
			mrules->head = mrules->tail = node;
		}
	}
	else
	{
		// hash string
		match_string *ms = malloc(sizeof(*ms));
		ms->s = strndup(str, len);
		tommy_hashdyn_insert(mrules->hash, &(ms->node), ms, tommy_strhash_u32(0, ms->s));
	}
}

void match_del(match_rules *mrules, char *str, size_t len)
{
	if(*str == '/')
	{	// chained regex
	}
	else
	{
		// hash string
		uint32_t str_hash = tommy_strhash_u32(0, str);
		match_string *ms = tommy_hashdyn_search(mrules->hash, match_mapper_compare, str, str_hash);
		tommy_hashdyn_remove_existing(mrules->hash, &(ms->node));

		free(ms->s);
		free(ms);
	}
}

void plain_parse(char *text, uint64_t size, char *sep, char *nlsep, char *prefix, uint64_t psize, context_arg *carg)
{
	if (!text)
		return;

	char *tmp = text;
	uint64_t next;
	char metric_name[PLAIN_METRIC_SIZE];
	uint64_t copysize;

	strlcpy(metric_name, prefix, psize+1);

	uint64_t i = 1000;
	char *prev = NULL;
	for (; (tmp-text) < size && i--; tmp += next)
	{
		if (prev == tmp)
			break;

		tmp += strspn(tmp, nlsep);
		next = strcspn(tmp, sep);

		copysize = next+1+psize > PLAIN_METRIC_SIZE ? PLAIN_METRIC_SIZE : next;
		metric_name_normalizer(tmp, copysize);
		//if (metric_name_validator(tmp, copysize))
			strlcpy(metric_name+psize, tmp, copysize+1);
		//else
		//{
		//	//strlcpy(metric_name+psize, tmp, copysize+1);
		//	//printf("metric name %s\n", metric_name);

		//	prev = tmp;
		//	next = strcspn(tmp, nlsep);
		//	continue;
		//}

		tmp += next;
		tmp += strspn(tmp, sep);

		next = strcspn(tmp, nlsep);
		copysize = next > PLAIN_METRIC_SIZE ? PLAIN_METRIC_SIZE : next;

		int rc = metric_value_validator(tmp, copysize);
		if (rc == DATATYPE_INT)
		{
			int64_t val = strtoll(tmp, NULL, 10);
			metric_add_auto(metric_name, &val, rc, carg);
		}
		else if (rc == DATATYPE_UINT)
		{
			uint64_t val = strtoull(tmp, NULL, 10);
			metric_add_auto(metric_name, &val, rc, carg);
		}
		else if (rc == DATATYPE_DOUBLE)
		{
			double val = strtod(tmp, NULL);
			metric_add_auto(metric_name, &val, rc, carg);
		}
		prev = tmp;
	}
}

string* get_file_content(char *file)
{
	FILE *fd = fopen(file, "r");
	if (!fd)
	{
		fprintf(stderr, "Open config file failed: %s", file);
		perror(": ");
		return 0;
	}

	fseek(fd, 0, SEEK_END);
	int64_t fdsize = ftell(fd);
	rewind(fd);

	char *buf = malloc(fdsize);
	size_t rc = fread(buf, 1, fdsize, fd);

	string *str = string_init_add(buf, rc, fdsize);
	return str;
}
