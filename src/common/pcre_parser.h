#pragma once
#include <pcre.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define REGEX_COUNTER 0
#define REGEX_INCREASE 1
#define REGEX_GAUGE 2

typedef struct regex_labels_node
{
	struct regex_labels_node *next;
	char *name;
	char *key;
} regex_labels_node;

typedef struct regex_labels
{
	regex_labels_node *head;
	regex_labels_node *tail;
	size_t size;
} regex_labels;

typedef struct regex_metric_node
{
	struct regex_metric_node *next;
	char *metric_name;
	regex_labels *labels;
	size_t labels_size;
	char *from;
	uint64_t value;
} regex_metric_node;

typedef struct regex_metric
{
	regex_metric_node *head;
	regex_metric_node *tail;
	size_t size;
	uint8_t type;
} regex_metric;

typedef struct regex_match
{	uint64_t nomatch;
	uint64_t null;
	uint64_t badoption;
	uint64_t badmagic;
	uint64_t unknown_node;
	uint64_t nomemory;
	uint64_t unknown_error;

	pcre_jit_stack* jstack;
	pcre *regex_compiled;
	pcre_extra *pcreExtra;

	regex_metric *metrics;
} regex_match;
