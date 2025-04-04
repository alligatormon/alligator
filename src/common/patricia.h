#pragma once
#include <inttypes.h>
#include "common/selector.h"
//__extension__ typedef unsigned __int128 uint128_t;
//__extension__ typedef signed __int128 int128_t;

typedef struct rnode {
    struct rnode *left;
    struct rnode *right;
    void *data;
} rnode;

typedef struct patricia_t {
    rnode *root;
} patricia_t;

rnode* network_add_ip(patricia_t *tree, patricia_t *tree6, char *s_ip, void *tag);
void* network_del_ip(patricia_t *tree, patricia_t *tree6, char *s_ip);
patricia_t *patricia_new();
uint32_t patricia_free(patricia_t *tree);
void *patricia_find(patricia_t *tree, uint32_t key, uint64_t *elem);
void *patricia_find128(patricia_t *tree, uint128_t key, uint64_t *elem);
uint128_t ip_to_integer(char *ip, uint8_t ip_version, char **ptr);
uint128_t grpow(uint128_t basis, uint64_t exponent);
void cidr_to_ip_and_mask(char *cidr, uint32_t *ip, uint32_t *mask);
patricia_t *patricia_tree_duplicate(patricia_t *src);
uint8_t ip_get_version(char *ip);
char* integer_to_ip(uint128_t ipaddr, uint8_t ip_version);
void cidr_to_ip_and_mask128(char *cidr, uint128_t *ip, uint128_t *mask);
