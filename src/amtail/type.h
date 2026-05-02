#pragma once

#include "dstructures/ht.h"
#include "jansson.h"
#include "events/context_arg.h"
#include "external/amtail/generator.h"
#include <uv.h>

typedef struct amtail_thread amtail_thread;

typedef struct amtail_node {
    char *name;
    char *key;
    char *script;
    amtail_bytecode *bytecode;
    string *tail;
    alligator_ht *variables;
    alligator_ht *labels;
	amtail_log_level amtail_ll;
    uv_mutex_t lock;
    /* One VM thread per compiled script — reused across UDP lines (avoids huge calloc/memset per line). */
    amtail_thread *vm_thread;
    tommy_node node;
} amtail_node;

int amtail_node_compare(const void* arg, const void* obj);
amtail_node *amtail_node_get(char *name);
amtail_node *amtail_node_get_any(void);
void amtail_node_free(amtail_node *an);
int amtail_init();
void amtail_free();
void amtail_parser_push();
int amtail_del(char *name);
int amtail_push(json_t *amtail);
void amtail_handler(char *metrics, size_t size, context_arg *carg);
