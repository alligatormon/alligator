#include <arpa/inet.h>
#include <time.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include "patricia.h"

patricia_t *patricia_new() {
    return calloc(1, sizeof(patricia_t));
}

rnode *patricia_node_create() {
    return calloc(1, sizeof(rnode));
}

rnode *patricia_insert(patricia_t *tree, uint32_t key, uint32_t mask, void *data) {
    if (!tree->root)
        tree->root = patricia_node_create();

    rnode *node = tree->root;
    rnode *next = node;

    uint32_t cursor = 0x80000000;

    while (cursor & mask) {
        if (key & cursor)
            next = node->right;
        else
            next = node->left;

        if (!next)
            break;

        cursor >>= 1;
        node = next;

    }

    if (next) {
        if (node->data) {
            return NULL;
        }

        node->data = data;
        return node;
    }

    while (cursor & mask) {
        next = patricia_node_create();

        if (key & cursor)
            node->right = next;
        else
            node->left = next;


        cursor >>= 1;
        node = next;
    }

    node->data = data;

    return node;
}

void *patricia_find(patricia_t *tree, uint32_t key, uint64_t *elem)
{
    rnode *node = tree->root;
    rnode *next = node;
    rnode *ret = NULL;

    uint32_t cursor = 0x80000000;

    while (next) {
        if (node->data) {
            ret = node;
        }

        if (key & cursor) {
            next = node->right;

        } else {
            next = node->left;
        }

        node = next;
        cursor >>= 1;
        ++(*elem);
    }

    return ret;
}

//uint32_t ip_to_integer32(char *ip, char **ptr) {
//        char *cur = ip;
//        uint32_t ret = 0;
//        uint16_t exp = 10;
//        int8_t blocks = 4;
//        uint32_t blocksize = 256;
//
//        while (--blocks >= 0)
//        {
//                uint16_t ochextet = strtoull(cur, &cur, exp);
//                uint8_t sep = strcspn(cur, ".:");
//                if (cur[sep] != '.')
//                        blocks = 0;
//
//                ret += (grpow(blocksize, blocks)*ochextet);
//
//                ++cur;
//        }
//
//        if (ptr)
//                *ptr = --cur;
//
//        return ret;
//}
//
uint128_t grpow(uint128_t basis, uint64_t exponent)
{
	uint128_t ret = basis;
	if (!exponent)
		return 1;
	else if (exponent == 1)
		return basis;

	while (--exponent)
		ret *= basis;

	return ret;
}


uint128_t ip_to_integer(char *ip, uint8_t ip_version, char **ptr)
{
	char *cur = ip;
	uint128_t ret = 0;
	uint16_t exp = 10;
	int8_t blocks = 4;
	uint32_t blocksize = 256;
	if (ip_version == 4)
	{
		exp = 10;
		blocksize= 256;
		blocks = 4;
	}
	if (ip_version == 6)
	{
		exp = 16;
		blocksize= 65536;
		blocks = 8;
	}

	while (--blocks >= 0)
	{
		uint16_t ochextet = strtoull(cur, &cur, exp);
		uint8_t sep = strcspn(cur, ".:");
		if (cur[sep] != '.' && cur[sep] != ':' )
			blocks = 0;

		ret += (grpow(blocksize, blocks)*ochextet);

		++cur;
	}

	if (ptr)
		*ptr = --cur; // TODO: Invalid write of size 8; freed in netlib.c:251

	return ret;
}

//uint32_t ip_get_range(uint8_t prefix)
//{
//        return grpow(2, (32 - prefix));
//}

uint32_t ip_get_mask(uint8_t prefix)
{
        return UINT_MAX - grpow(2, 32 - prefix) + 1;
}

void cidr_to_ip_and_mask(char *cidr, uint32_t *ip, uint32_t *mask)
{
        char *pref;
        *ip = ip_to_integer(cidr, 4, &pref);
        uint8_t prefix;
        if (pref && *pref == '/')
                prefix = strtoull(++pref, NULL, 10);
        else
                prefix = 32;

        if (mask)
            *mask = ip_get_mask(prefix);

        //if (size)
        //    *size = ip_get_range(prefix);
}

void check_ip(patricia_t *tree, char *ip) {
    uint32_t int_ip;

    cidr_to_ip_and_mask(ip, &int_ip, NULL);
    uint64_t t;
    rnode *node = patricia_find(tree, int_ip, &t);
    if (node && node->data)
        printf("network for %s: %s\n", ip, (char*)node->data);
    else
        printf("can't find %s\n", ip);
}

rnode* network_add_ip(patricia_t *tree, char *s_ip, void *tag) {
    uint32_t mask;
    uint32_t ip;
    cidr_to_ip_and_mask(s_ip, &ip, &mask);
    return patricia_insert(tree, ip, mask, tag);
}

uint32_t fill_networks (patricia_t *tree, char *dir) {
    struct dirent *ipdirent;
    DIR *ipdir = opendir(dir);
    if (!ipdir) {
        fprintf(stderr, "cannot open dir %s\n", dir);
        return 0;
    }

    uint32_t count = 0;
    char fname[255];
    size_t dirlen = strlen(dir);
    strcpy(fname, dir);
    while ((ipdirent = readdir(ipdir))) {
        char *countryname = strndup(ipdirent->d_name, strcspn(ipdirent->d_name, "."));

        strcpy(fname+dirlen, ipdirent->d_name);
        FILE *fd = fopen(fname, "r");
        if (!fd) {
            fprintf(stderr, "can't open file %s\n", fname);
            break;
        }

        char buf[64];
        while (fgets(buf, 63, fd)) {
            buf[strlen(buf) - 1] = '\0';
            //printf("add %s->%s\n", buf, countryname);
            if(network_add_ip(tree, buf, countryname))
                ++count;
        }
    }

    return count;
}

void print_node(rnode *node, int indent, uint32_t cursor) {
    uint32_t pass_cursor = cursor >> 1;

    if (node->right)
        print_node(node->right, indent+1, pass_cursor);

    char *bcursor = (char*)&cursor;
    for (int i = 0; i < indent; printf(" "), ++i);
    printf("cursor %x, %hhu.%hhu.%hhu.%hhu: %s\n", cursor, bcursor[3], bcursor[2], bcursor[1], bcursor[0], node->data ? (char*)node->data : "");

    if (node->left)
        print_node(node->left, indent+1, pass_cursor);
}

void print_tree(patricia_t *tree) {
    uint32_t cursor = 0x80000000;

    print_node(tree->root, 0, cursor);
}

//int main() {
//    patricia_t *tree = patricia_new();
//
//    add_ip(tree, "1.97.34.0/24", "testnet");
//    add_ip(tree, "127.0.0.0/8", "localnet");
//    add_ip(tree, "192.168.0.0/16", "homenet");
//    add_ip(tree, "172.16.0.0/12", "compnet");
//    add_ip(tree, "10.0.0.0/8", "corpnet");
//    print_tree(tree);
//
//    uint32_t added = fill_networks(tree, "country-ip-blocks/ipv4/");
//    printf("%u networks were added\n", added);
//
//    uint64_t elem = 0;
//    for (uint32_t a = 0; a < 16777216; ++a) {
//        uint32_t int_ip = a;
//        rnode *node = patricia_find(tree, int_ip, &elem);
//    }
//    printf("elapsed: %lu\n", elem);
//
//    check_ip(tree, "1.97.32.34");
//    check_ip(tree, "139.15.177.136");
//    check_ip(tree, "209.85.233.102");
//    check_ip(tree, "127.1.23.23");
//    check_ip(tree, "172.17.198.23");
//}
