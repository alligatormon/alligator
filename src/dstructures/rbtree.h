#pragma once
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#include <ctype.h>
#include "tommyds/tommyds/tommy.h"
#include "common/rtime.h"
#include "common/selector.h"

#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>
#endif
#define MAX_LEN	100000
#define NAMELEN 1000
#define u64	PRIu64
#define d64	PRId64
#define	RED	1
#define	BLACK	0
#define	RIGHT	1
#define	LEFT	0
typedef struct xstack
{
	struct xstack *next;
	struct xstack *prev;
	double d;
} xstack;
typedef struct xsstack
{
	xstack *first;
	xstack *last;
	size_t c;
	size_t cm;
} xsstack;
typedef struct alligator_labels
{
	char *name;
	char *key;
	struct alligator_labels *next;
	xsstack *xss;
} alligator_labels;

typedef struct rb_node 
{
	int color;
	char *key;
	char *ts_file;
	double d;
	int64_t i;
	char *s;
	struct rb_node *steam[2];
	struct rb_tree *stree;
	char *name;
	//double val;
	int8_t en;
	r_time timestamp;
} rb_node;

typedef struct rb_tree 
{
	struct rb_node *root;
	char *name;
	int64_t count;
//	kv_ngram *hash;
	tommy_hashdyn *hash;
} rb_tree;

rb_node* rb_insert ( rb_tree *tree, alligator_labels *lbl );
rb_node* rb_find ( rb_tree *tree, char* key );
//void rb_tree_labels_for(rb_node *x, char *prefix, int slave, stlen *str);
void rb_tree_labels_for(rb_node *x, char *prefix, int slave, stlen *str);
