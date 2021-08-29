#pragma once
#include <stdio.h>
#include <inttypes.h>
#include <pcre.h>
#include "dstructures/ht.h"

typedef struct stlen
{
	char *s;
	size_t l;
} stlen;

typedef struct mtlen
{
	stlen *st;
	size_t m;
	size_t max;
} mtlen;

typedef struct string {
	char *s;
	size_t l;
	size_t m;
} string;

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
int64_t str_get_next(char *buf, char *ret, uint64_t ret_sz, char *sep, uint64_t *cursor);
int64_t int_get_next(char *buf, size_t sz, char sep, uint64_t *cursor);
void selector_get_plain_metrics(char *m, size_t ms, char *sep, char *msep, char *prefix, size_t prefix_size);
size_t get_file_size(char *filename);
char* selector_getline( char *str, size_t str_n, char *fld, size_t fld_len, uint64_t num );
void stlentext(stlen *str, char *str2);
void stlencat(stlen *str, char *str2, size_t len);
mtlen* split_char_to_mtlen(char *str);
char *gettextfile(char *path, size_t *filesz);
char* selector_get_field_by_str(char *str, size_t str_n, char *sub, int col, char *sep);
int64_t getkvfile(char *file);
string* string_init_str(char *str, size_t max);
string* string_init(size_t max);
void string_null(string *str);
string* string_init_alloc(char *str, size_t max);
string* string_init_add(char *str, size_t len, size_t max);
string* get_file_content(char *file);
string* string_new();
void string_free_callback(char *data);
string* string_init_add_auto(char *str);
string* string_init_dup(char *str);
