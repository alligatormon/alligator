#pragma once
#include <stdio.h>
#include <inttypes.h>
#include <pcre.h>
#include "dstructures/ht.h"
#include <jansson.h>
#include <byteswap.h>
typedef __int128 int128_t;
typedef unsigned __int128 uint128_t;

void json_array_object_insert(json_t *dst_json, char *key, json_t *src_json);;

typedef struct string {
	char *s;
	size_t l;
	size_t m;
} string;

typedef struct string_tokens {
	string **str;
	uint64_t l;
	uint64_t m;
} string_tokens;

typedef struct match_string {
	char *s;
	uint64_t count;
	tommy_node node;
} match_string;

typedef struct regex_list {
	char *name;
	uint64_t count;
	pcre *re_compiled;
	pcre_extra *pcre_extra;
	pcre_jit_stack *jstack;
	struct regex_list *next;
} regex_list;

typedef struct match_rules
{
	alligator_ht *hash;
	regex_list *head;
	regex_list *tail;
} match_rules;

double double_get_next(char *buf, char *sep, uint64_t *cursor);
int64_t str_get_next(const char *buf, char *ret, uint64_t ret_sz, char *sep, uint64_t *cursor);
int64_t int_get_next(char *buf, size_t sz, char sep, uint64_t *cursor);
int64_t uint_get_next(char *buf, size_t sz, char sep, uint64_t *cursor);
void selector_get_plain_metrics(char *m, size_t ms, char *sep, char *msep, char *prefix, size_t prefix_size);
size_t get_file_size(const char *filename);
char* selector_getline( char *str, size_t str_n, char *fld, size_t fld_len, uint64_t num );
char *gettextfile(char *path, size_t *filesz);
char* selector_get_field_by_str(char *str, size_t str_n, char *sub, int col, char *sep);
int64_t getkvfile(char *file);
int64_t getkvfile_ext(char *file, uint8_t *err);
int getkvfile_str(char *file, char *str, uint64_t size);
int64_t getkvfile_uint(char *file);
void str_tolower(char *str, size_t size);
string* string_init_str(char *str, size_t max);
string* string_init(size_t max);
void string_cat(string *str, char *strcat, size_t len);
void string_null(string *str);
void string_number(string *str, void* value, int8_t type);
int string_compare(string *str, char *cmp, uint64_t len);
string* string_init_alloc(char *str, size_t max);
string* string_init_add(char *str, size_t len, size_t max);
string* string_string_init_dup(string *str);
string* get_file_content(char *file, uint8_t error_logging);
string* string_new();
void string_free(string *str);
void string_free_callback(char *data);
string* string_init_add_auto(char *str);
string* string_init_dup(char *str);
string* string_init_dupn(char *str, uint64_t size);
void string_break(string *str, uint64_t start, uint64_t end);
void string_copy(string *dst, char *src, uint64_t len);
void string_uint(string *str, uint64_t u);
void string_int(string *str, int64_t i);
void string_double(string *str, double d);
void string_string_cat(string *str, string *src);
void string_string_copy(string *dst, string *src);
void string_merge(string *str, string *src);

uint8_t string_tokens_push(string_tokens *st, char *s, uint64_t l);
uint8_t string_tokens_string_push(string_tokens *st, string *str);
string_tokens *string_tokens_new();
void string_tokens_free(string_tokens *st);
uint8_t string_tokens_push_dupn(string_tokens *st, char *s, uint64_t l);
string* string_tokens_join(string_tokens *st, char *sepsym, uint64_t seplen);
json_t* string_tokens_json(string_tokens *st);

int sisdigit(const char *str);
char *trim_whitespaces(char *str);
char *trim(char *s);
uint64_t get_file_atime(char *filename);
int8_t match_mapper(match_rules *mrules, char *str, size_t size, char *name);
void plain_parse(char *text, uint64_t size, char *sep, char *nlsep, char *prefix, uint64_t psize, void *carg);
uint64_t selector_count_field(char *str, char *pattern, uint64_t sz);
void match_push(match_rules *mrules, char *str, size_t len);
void match_del(match_rules *mrules, char *str, size_t len);
void to_lower_before(char *s, char *before);
void match_free(match_rules *mrules);
uint64_t read_all_file(char *filename, char **buf);
uint64_t count_file_lines(char *filename);
size_t strcspn_n(const char *s, const char *find, size_t max);

uint128_t to_uint128(char data[]);
uint64_t to_uint64(char data[]);
uint64_t to_uint64_swap(char data[]);
uint16_t to_uint16(char data[]);
uint16_t to_uint16_swap(char data[]);
uint32_t to_uint32(char data[]);
uint32_t to_uint32_swap(char data[]);
