#pragma once
#include <stdio.h>
#include <inttypes.h>
#include <pcre.h>
#include "dstructures/tommy.h"
#include "events/context_arg.h"

typedef struct stlen
{
	char *s;
	size_t l;
} stlen;

typedef struct mtlen
{
	stlen *st;
	size_t m;
} mtlen;

typedef struct string {
	char *s;
	size_t l;
	size_t m;
} string;

typedef struct match_string {
	char *s;
	tommy_node node;
} match_string;

typedef struct regex_list {
	pcre *re_compiled;
	pcre_extra *pcre_extra;
	pcre_jit_stack *jstack;
	struct regex_list *next;
} regex_list;

typedef struct match_rules
{
	tommy_hashdyn *hash;
	regex_list *head;
	regex_list *tail;
} match_rules;

void selector_get_plain_metrics(char *m, size_t ms, char *sep, char *msep, char *prefix, size_t prefix_size);
size_t get_file_size(char *filename);
char* selector_getline( char *str, size_t str_n, char *fld, size_t fld_len, uint64_t num );
void stlentext(stlen *str, char *str2);
void stlencat(stlen *str, char *str2, size_t len);
mtlen* split_char_to_mtlen(char *str);
char *gettextfile(char *path, size_t *filesz);
char* selector_split_metric(char *text, size_t sz, char *nsep, size_t nsep_sz, char *sep, size_t sep_sz, char *prefix, size_t prefix_size, char **maps, size_t maps_size, context_arg *carg);
char* selector_get_field_by_str(char *str, size_t str_n, char *sub, int col, char *sep);
int64_t getkvfile(char *file);
string* string_init_str(char *str, size_t max);
string* string_init(size_t max);
