#pragma once
#include <inttypes.h>
__extension__ typedef unsigned __int128 uint128_t;

typedef struct rnode {
    struct rnode *left;
    struct rnode *right;
    void *data;
} rnode;

typedef struct patricia_t {
    rnode *root;
} patricia_t;

rnode* network_add_ip(patricia_t *tree, char *s_ip, void *tag);
patricia_t *patricia_new();
void *patricia_find(patricia_t *tree, uint32_t key, uint64_t *elem);
uint128_t ip_to_integer(char *ip, uint8_t ip_version, char **ptr);
uint128_t grpow(uint128_t basis, uint64_t exponent);
