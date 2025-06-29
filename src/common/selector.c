#include <string.h>
#include <pcre.h>
#include "dstructures/tommy.h"
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "common/validator.h"
//#include "platform/platform.h"
#include "main.h"
#include "metric/namespace.h"

#define PLAIN_METRIC_SIZE 1000

size_t get_file_size(const char *filename)
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

uint64_t count_nl(char *buffer, uint64_t size) {
	uint64_t chars = 0;
	for(uint64_t i = 0; buffer[i] != 0 && i < size; ++i)
		if (buffer[i] == '\n')
			++chars;
	return chars;
}

uint64_t count_file_lines(char *filename) {
	uint64_t charcount = 0;
	int fd = open(filename, O_RDONLY);
	if (!fd)
		return 0;

	char buffer[65535];
	int64_t ret = 0;
	while ((ret = read(fd, buffer, 65534)) > 0)
	{
		buffer[ret] = 0;
		charcount += count_nl(buffer, ret);
	}
	close(fd);

	return charcount;
}

uint64_t get_file_atime(char *filename)
{
	struct stat st = {0};
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
					strncat(ret, "\n", 2);
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

uint64_t selector_count_field(char *str, char *pattern, uint64_t sz)
{
	//printf("selector_count_field '%s' with size %d and pattern '%s'\n", str, sz, pattern);
	uint64_t cnt;
	uint64_t i;
	for (cnt = 0, i = 0; i < sz; i++, ++cnt)
		i += strcspn(str+i, pattern);
	//printf("cnt is %d\n", cnt);
	return cnt;
}

int sisdigit(const char *str)
{
	uint8_t dot = 0;
	for (uint64_t i = 0; str[i] != 0; ++i)
	{
		if (!isdigit(str[i]))
			return 0;
		else if ((str[i] == '.') && (!dot))
			dot = 1;
		else if ((str[i] == '.') && (dot))
			return 0;
	}

	return 1;
}

char *ltrim(char *s)
{
	while(isspace(*s)) s++;
	return s;
}

char *rtrim(char *s)
{
	char* back = s + strlen(s);
	while(isspace(*--back));
	*(back+1) = '\0';
	return s;
}

char *trim(char *s)
{
	return rtrim(ltrim(s));
}

inline size_t strcspn_n(const char *s, const char *find, size_t max) {
	size_t ret = strcspn(s, find);
	return ret > max ? max : ret;
}

int64_t int_get_next(char *buf, size_t sz, char sep, uint64_t *cursor)
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

double double_get_next(char *buf, char *sep, uint64_t *cursor)
{
	uint64_t end = strcspn(buf + *cursor, sep);
	double ret = strtod(buf + *cursor, NULL);

	//printf("cursor %d, end %d, ret %lf, '%s'\n", *cursor, end, ret, buf + *cursor);
	*cursor += end;

	return ret;
}

int64_t str_get_next(const char *buf, char *ret, uint64_t ret_sz, char *sep, uint64_t *cursor)
{
	uint64_t end = strcspn(buf + *cursor, sep);
	//printf("end is %"u64": '%s' (buf+%"u64"), find:('%s')\n", end, buf + *cursor, *cursor, sep);
	uint64_t copysize = (end + 1) > ret_sz ? ret_sz : (end + 1);
	//printf("copysize %"u64": (%d)\n", copysize, ((end + 1) > ret_sz));

	strlcpy(ret, buf + *cursor, copysize);
	(*cursor) += end;

	return end;
}

int64_t uint_get_next(char *buf, size_t sz, char sep, uint64_t *cursor)
{
	for (; *cursor<sz; ++(*cursor))
	{
		for (; *cursor<sz && buf[*cursor]==sep; ++(*cursor));
		if (isdigit(buf[*cursor]) || buf[*cursor] == '-')
		{
			int64_t ret = strtoull(buf+(*cursor), NULL, 10);
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

int64_t getkvfile_uint(char *file)
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

	return strtoull(temp, NULL, 10);
}

int64_t getkvfile_ext(char *file, uint8_t *err)
{
	char temp[20];
	*err = 0;
	FILE *fd = fopen(file, "r");
	if (!fd)
	{
		*err = 1;
		return 0;
	}

	if ( !fgets(temp, 20, fd) )
	{
		fclose(fd);
		*err = 2;
		return 0;
	}

	fclose(fd);

	return atoll(temp);
}

int getkvfile_str(char *file, char *str, uint64_t size)
{
	FILE *fd = fopen(file, "r");
	if (!fd)
		return 0;

	if ( !fgets(str, size, fd) )
	{
		fclose(fd);
		return 0;
	}

	str[strcspn(str, "\r\n")] = 0;

	fclose(fd);
	return 1;
}

void str_tolower(char *str, size_t size)
{
	for(int i = 0; str[i] && i < size; i++)
		str[i] = tolower(str[i]);
}


string* string_init(size_t max)
{
	++max;
	string *ret = malloc(sizeof(*ret));
	ret->m = max;
	ret->s = malloc(max);
	*ret->s = 0;
	ret->l = 0;
	//printf("alloc: %p\n", ret->s);

	return ret;
}

string* string_new()
{
	string *ret = malloc(sizeof(*ret));
	ret->m = 0;
	ret->s = NULL;
	ret->l = 0;

	return ret;
}

void string_new_size(string *str, size_t len)
{
	if (!str->m)
	{
		str->m = len + 1;
		str->s = malloc(str->m);
		str->l = 0;
		*str->s = 0;
		return;
	}
	uint64_t newsize = str->m*2+len;
	//printf("alloc %zu, copy %zu, str->m %zu, str->m*2 %zu, str->m*2+len %zu, len %zu\n", newsize, str->l+1, str->m, str->m*2, str->m*2+len, len);
	char *newstr_char = malloc(newsize);
	memcpy(newstr_char, str->s, str->l+1);
	char *oldstr_char = str->s;
	str->s = newstr_char;
	str->m = newsize;

	free(oldstr_char);
}

void string_null(string *str)
{
	if (str->s)
		*str->s = 0;
	str->l = 0;
}

void string_free(string *str)
{
	//printf("free: %p\n", str->s);
	if (str->s)
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
	ret->s[len] = 0;

	return ret;
}


string* string_init_add_auto(char *str)
{
	string *ret = malloc(sizeof(*ret));
	ret->m = ret->l = strlen(str);
	ret->s = str;

	return ret;
}

string* string_init_dup(char *str)
{
	string *ret = malloc(sizeof(*ret));
	ret->m = ret->l = strlen(str);
	ret->s = strdup(str);

	return ret;
}

string* string_init_dupn(char *str, uint64_t size)
{
	string *ret = malloc(sizeof(*ret));
	ret->m = ret->l = size;
	ret->s = strndup(str, size);

	return ret;
}

string* string_string_init_dup(string *str)
{
	string *ret = malloc(sizeof(*ret));
	ret->m = ret->l = str->l;
	ret->s = strdup(str->s);

	return ret;
}

void string_cat(string *str, char *strcat, size_t len)
{
	size_t str_len = str->l;
	//printf("1212str_len=%zu\n", str_len);
	if(str_len+len >= str->m)
		string_new_size(str, len);

	size_t copy_size = len < (str->m - str_len) ? len : str_len;
	//printf("+++++ string cat (%zu) +++++++\n%s\n", len, strcat);
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

void string_copy(string *dst, char *src, uint64_t src_len)
{
	if (src_len >= dst->m)
		string_new_size(dst, src_len);

	memcpy(dst->s, src, src_len);
	dst->l = src_len;
	dst->s[dst->l] = 0;
}

void string_string_copy(string *dst, string *src)
{
	size_t src_len = src->l;

	if (src_len >= dst->m)
		string_new_size(dst, src_len);

	memcpy(dst->s, src->s, src_len);
	dst->l = src_len;
	dst->s[dst->l] = 0;
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

void string_number(string *str, void* value, int8_t type)
{
	if (type == DATATYPE_INT)
		string_int(str, *(int64_t*)value);
	else if (type == DATATYPE_UINT)
		string_uint(str, *(uint64_t*)value);
	else if (type == DATATYPE_DOUBLE)
		string_double(str, *(double*)value);
}

string* string_init_alloc(char *str, size_t max)
{
	if (!max)
		max = strlen(str);
	string *ret = malloc(sizeof(*ret));
	ret->m = max;
	ret->s = malloc(max+1);
	memcpy(ret->s, str, max);
	ret->s[max] = 0;
	ret->l = max;

	return ret;
}

void string_cut(string *str, uint64_t offset, size_t len)
{
	//printf("================%"u64":%zu:%zu================\n", offset, len, str->l);
	//printf("'%s'\n", str->s + offset);
	//puts("--------------------------------");
	if (!str)
		return;

	//if (offset == str->l)
	//	return;

	if (!len)
		return;

	if (!str->l)
		return;

	memcpy(str->s + offset, str->s + offset + len, str->l - len - offset);
	str->l -= len;
	str->s[str->l] = 0;
	printf("'%s'\n", str->s + offset);
	puts("================================");
}

void string_break(string *str, uint64_t start, uint64_t end)
{
	uint64_t newend = end - start;
	//printf("1start is %"u64", end is %"u64", newend is %"u64", l is %zu, m is %zu\n", start, end, newend, str->l, str->m);
	if (!end)
		newend = str->l - start;
	//printf("2start is %"u64", end is %"u64", newend is %"u64", l is %zu, m is %zu\n", start, end, newend, str->l, str->m);

	if (start)
		memcpy(str->s, str->s + start, newend);
	str->l = newend;
	str->s[str->l] = 0;
}

int string_compare(string *str, char *cmp, uint64_t len)
{
	if (!str)
		return -1;

	if (!str->l)
		return -1;

	if (!cmp)
		return 1;

	if (!len)
		return 1;

	return strncmp(str->s, cmp, len);
}

void string_free_callback(char *data)
{
	string *str = (string*)data;
	string_free(str);
}

string_tokens *string_tokens_new()
{
	string_tokens *ret = calloc(1, sizeof(*ret));

	return ret;
}

void string_tokens_scale(string_tokens *st)
{
	if (!st)
		return;

	uint64_t max = st->m ? st->m * 2 : 2;

	string **new = calloc(1, sizeof(**new) * max);
	memcpy(new, st->str, st->l * sizeof(string));

	string **old = st->str;
	st->str = new;

	if (old)
		free(old);

	st->m = max;
}

uint8_t string_tokens_push(string_tokens *st, char *s, uint64_t l)
{
	if (!st)
		return  0;

	if (!st->m)
		string_tokens_scale(st);

	if (st->m <= st->l)
		string_tokens_scale(st);

	st->str[st->l] = string_init_add(s, l, l);

	++st->l;

	return 1;
}

uint8_t string_tokens_string_push(string_tokens *st, string *str)
{
	if (!st)
		return  0;

	if (!st->m)
		string_tokens_scale(st);

	if (st->m <= st->l)
		string_tokens_scale(st);

	st->str[st->l] = str;

	++st->l;

	return 1;
}

uint8_t string_tokens_push_dupn(string_tokens *st, char *s, uint64_t l)
{
	if (!st)
		return  0;

	if (!st->m)
		string_tokens_scale(st);

	if (st->m <= st->l)
		string_tokens_scale(st);

	st->str[st->l] = string_init_dupn(s, l);

	++st->l;

	return 1;
}

string* string_tokens_join(string_tokens *st, char *sepsym, uint64_t seplen)
{
	string *joined = string_new();
	for (uint64_t i = 0; i < st->l; i++)
	{
		if (i)
			string_cat(joined, sepsym, seplen);
		string_string_cat(joined, st->str[i]);
	}
	return joined;
}

json_t* string_tokens_json(string_tokens *st)
{
	json_t *array = json_array();
	for (uint64_t i = 0; i < st->l; i++)
	{
		json_t *token = json_string(st->str[i]->s);
		json_array_object_insert(array, "", token);
	}
	return array;
}

void string_tokens_free(string_tokens *st)
{
	for (uint64_t i = 0; i < st->l; i++)
	{
		string_free(st->str[i]);
	}
	free(st->str);
	free(st);
}

int match_mapper_compare(const void* arg, const void* obj)
{
        char *s1 = (char*)arg;
        char *s2 = ((match_string*)obj)->s;
        return strcmp(s1, s2);
}

// return 0 if not matched
// return 1 if matched
// return 2 if matching all
int8_t match_mapper(match_rules *mrules, char *str, size_t size, char *name)
{
	int64_t n = alligator_ht_count(mrules->hash);
	if ((!n) && (!mrules->head))
	{
		return 2;
	}

	match_string *ms = alligator_ht_search(mrules->hash, match_mapper_compare, str, tommy_strhash_u32(0, str));
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
		alligator_ht_insert(mrules->hash, &(ms->node), ms, tommy_strhash_u32(0, ms->s));
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
		match_string *ms = alligator_ht_search(mrules->hash, match_mapper_compare, str, str_hash);
		alligator_ht_remove_existing(mrules->hash, &(ms->node));

		free(ms->s);
		free(ms);
	}
}

void match_foreach(void *arg)
{
	match_string *ms = arg;
	free(ms->s);
	free(ms);
}

void match_free(match_rules *mrules)
{
	alligator_ht_foreach(mrules->hash, match_foreach);

	regex_list *node = mrules->head;
	while (node != mrules->tail)
	{
		free(node->name);
		regex_list *prev = node;
		node = node->next;
		free(prev);
	}

	alligator_ht_done(mrules->hash);
	free(mrules->hash);
	free(mrules);
}

void plain_parse(char *text, uint64_t size, char *sep, char *nlsep, char *prefix, uint64_t psize, void *arg)
{
	context_arg *carg = arg;
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

string* get_file_content(char *file, uint8_t error_logging)
{
	FILE *fd = fopen(file, "r");
	if (!fd)
	{
		if (error_logging)
		{
			fprintf(stderr, "Open config file failed: %s", file);
			perror(": ");
		}
		return 0;
	}

	fseek(fd, 0, SEEK_END);
	int64_t fdsize = ftell(fd);
	rewind(fd);

	char *buf = malloc(fdsize+1);
	size_t rc = fread(buf, 1, fdsize, fd);
	buf[rc] = 0;

	string *str = string_init_add(buf, rc, fdsize);

	fclose(fd);

	return str;
}

void to_lower_s(char *s, size_t l)
{
	for ( ; *s; ++s, --l) *s = tolower(*s);
}

void to_lower_before(char *s, char *before)
{
	to_lower_s(s, strcspn(s, before));
}

char *trim_whitespaces(char *str)
{
	char *end;

	while(isspace((unsigned char)*str)) str++;

	if(*str == 0)
	  return str;

	end = str + strlen(str) - 1;
	while(end > str && isspace((unsigned char)*end)) end--;

	end[1] = '\0';

	return str;
}

uint128_t to_uint128(char data[]) {
	char kvalue[16] = { 0 };
	memcpy(kvalue, data, 16);
	uint128_t *kdvalue = (uint128_t*)kvalue;
	return *kdvalue;
}

uint64_t to_uint64(char data[]) {
	char kvalue[8] = { 0 };
	memcpy(kvalue, data, 8);
	uint64_t *kdvalue = (uint64_t*)kvalue;
	return *kdvalue;
}

uint64_t to_uint64_swap(char data[]) {
	char kvalue[8] = { 0 };
	memcpy(kvalue, data, 8);
	uint64_t *kdvalue = (uint64_t*)kvalue;
	uint64_t swapvalue = bswap_64(*kdvalue);
	return swapvalue;
}

uint16_t to_uint16(char data[]) {
	char kvalue[2] = { 0 };
	memcpy(kvalue, data, 2);
	uint16_t *kdvalue = (uint16_t*)kvalue;
	return *kdvalue;
}

uint16_t to_uint16_swap(char data[]) {
	char kvalue[2] = { 0 };
	memcpy(kvalue, data, 2);
	uint16_t *kdvalue = (uint16_t*)kvalue;
	uint16_t swapvalue = bswap_16(*kdvalue);
	return swapvalue;
}

uint32_t to_uint32(char data[]) {
	char kvalue[4] = { 0 };
	memcpy(kvalue, data, 4);
	uint32_t *kdvalue = (uint32_t*)kvalue;
	return *kdvalue;
}

uint32_t to_uint32_swap(char data[]) {
	char kvalue[4] = { 0 };
	memcpy(kvalue, data, 4);
	uint32_t *kdvalue = (uint32_t*)kvalue;
	uint32_t swapvalue = bswap_32(*kdvalue);
	return swapvalue;
}
