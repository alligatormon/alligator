#pragma once
#include "dstructures/ht.h"
typedef struct eventlog_node
{
    char key[256];
    char resource[256];
    char state[256];
    uint64_t val;

    tommy_node node;
} eventlog_node;

int eventlog_node_compare(const void* arg, const void* obj);
