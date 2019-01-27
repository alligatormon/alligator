#pragma once
#include <stdio.h>
#include <inttypes.h>

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

void selector_get_plain_metrics(char *m, size_t ms, char *sep, char *msep, char *prefix, size_t prefix_size);
size_t get_file_size(char *filename);
char* selector_getline( char *str, size_t str_n, char *fld, size_t fld_len, uint64_t num );
void stlentext(stlen *str, char *str2);
void stlencat(stlen *str, char *str2, size_t len);
mtlen* split_char_to_mtlen(char *str);
