#include <arpa/inet.h>
#include <time.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include "patricia.h"
#include "common/logs.h"

#define INT128_MAX (__int128)(((unsigned __int128) 1 << ((sizeof(__int128) * __CHAR_BIT__) - 1)) - 1)
#define UINT128_MAX ((2 * (unsigned __int128) INT128_MAX) + 1)

#define PATRICIA_RIGHT 0
#define PATRICIA_LEFT 1

patricia_t *patricia_new() {
    return calloc(1, sizeof(patricia_t));
}

rnode *patricia_node_create() {
    return calloc(1, sizeof(rnode));
}

uint8_t ip_get_version(char *ip)
{
	uint64_t delim = strcspn(ip, ":.");
	uint8_t ip_version = 0;
	if (ip[delim] == '.')
		ip_version = 4;
	else if (ip[delim] == ':')
		ip_version = 6;
	else
		return 0;

	return ip_version;
}

char* integer_to_ip(uint128_t ipaddr, uint8_t ip_version)
{
	char *ip = calloc(1, 40);
	char *cur = ip;
	if (ip_version == 4)
	{
		int8_t blocks = 4;
		while (--blocks >= 0)
		{
			uint8_t number = 0;
			uint128_t select_octet = grpow(256, blocks);
			if (ipaddr >= select_octet)
			{
				number = ipaddr/select_octet;
				ipaddr -= (ipaddr/select_octet)*select_octet;
				int syms = sprintf(cur, "%"PRIu8, number);
				if (syms>0)
					cur += syms;
			}
			else
			{
				strcat(cur++, "0");
			}

			if (blocks != 0)
			{
				strcat(cur++, ".");
			}
		}
	}
	else if (ip_version == 6)
	{
		int8_t blocks = 8;
		while (--blocks >= 0)
		{
			uint16_t number = 0;
			uint128_t select_hextet = grpow(65536, blocks);
			if (ipaddr >= select_hextet)
			{
				number = ipaddr/select_hextet;
				ipaddr -= (ipaddr/select_hextet)*select_hextet;
				int syms = sprintf(cur, "%"PRIu16, number);
				if (syms>0)
					cur += syms;
			}
			else
			{
				strcat(cur++, "0");
			}

			if (blocks != 0)
			{
				strcat(cur++, ":");
			}
		}
	}
	else
	{
		free(ip);
		return NULL;
	}

	return ip;
}


rnode *patricia_insert(patricia_t *tree, uint32_t key, uint32_t mask, void *data, char *s_ip) {
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
            if ((key != 0) && (mask != 0)) {
                glog(L_WARN, "patricia_insert warning: the same address already had been added to ACL: '%s'", s_ip);
            }
            free(node->data);
            node->data = data;
            return node;
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

rnode *patricia_insert128(patricia_t *tree, uint128_t key, uint128_t mask, void *data, char *s_ip) {
    if (!tree->root)
        tree->root = patricia_node_create();

    rnode *node = tree->root;
    rnode *next = node;

    uint128_t cursor = (__uint128_t)0x8000000000000000 << 64 | 0x0;

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
            if ((key != 0) && (mask != 0)) {
                glog(L_WARN, "patricia_insert128 warning: the same address already had been added to ACL: '%s'", s_ip);
            }
            free(node->data);
            node->data = data;
            return node;
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

void* patricia_delete(patricia_t *tree, uint32_t key, uint32_t mask, char *s_ip) {
    if (!tree->root)
        return NULL;

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
            if ((key != 0) && (mask != 0)) {
                return node->data;
            }
            else {
                glog(L_WARN, "patricia_insert warning: the same address already had been added to ACL: '%s'", s_ip);
                return NULL;
            }
        }
    }

    return NULL;
}

void* patricia_delete128(patricia_t *tree, uint128_t key, uint128_t mask, char *s_ip) {
    if (!tree->root)
        return NULL;

    rnode *node = tree->root;
    rnode *next = node;

    uint128_t cursor = (__uint128_t)0x8000000000000000 << 64 | 0x0;

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
            if ((key != 0) && (mask != 0)) {
                return node->data;
            }
            else {
                glog(L_WARN, "patricia_insert warning: the same address already had been added to ACL: '%s'", s_ip);
                return NULL;
            }
        }
    }

    return NULL;
}



uint32_t patricia_node_free(rnode *node) {
    if (!node)
        return 0;

    uint32_t count = 0;

    if (node->right)
        count += patricia_node_free(node->right);

    if (node->left)
        count += patricia_node_free(node->left);

    if (node->data) {
        ++count;
        free(node->data);
    }

    free(node);

    return count;
}

uint32_t patricia_free(patricia_t *tree) {
    uint32_t count = 0;

    if (!tree || !tree->root)
        return count;

    count = patricia_node_free(tree->root);

    free(tree);
    return count;
}

void patricia_node_duplicate(rnode *snode, rnode *dnode, int branch_direction) {

    rnode *branch = NULL;
    if (branch_direction == PATRICIA_RIGHT)
        branch = dnode->right = patricia_node_create();
    else if (branch_direction == PATRICIA_LEFT)
        branch = dnode->left = patricia_node_create();

    if (snode->right)
        patricia_node_duplicate(snode->right, branch, PATRICIA_RIGHT);
    if (snode->left)
        patricia_node_duplicate(snode->left, branch, PATRICIA_LEFT);

    if (snode->data) {
        branch->data = calloc(1, sizeof(uint8_t));
        memcpy(branch->data, snode->data, sizeof(uint8_t));
    }
}

patricia_t *patricia_tree_duplicate(patricia_t *src) {
    if (!src)
        return src;

    patricia_t *dst = patricia_new();

    if (!src->root)
        return dst;

    rnode *snode = src->root;
    rnode *dnode = dst->root = patricia_node_create();
    if (snode->right)
        patricia_node_duplicate(snode->right, dnode, PATRICIA_RIGHT);
    if (snode->left)
        patricia_node_duplicate(snode->left, dnode, PATRICIA_LEFT);

    if (snode->data) {
        dnode->data = calloc(1, sizeof(uint8_t));
        memcpy(dnode->data, snode->data, sizeof(uint8_t));
    }

    return dst;
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

void *patricia_find128(patricia_t *tree, uint128_t key, uint64_t *elem)
{
    rnode *node = tree->root;
    rnode *next = node;
    rnode *ret = NULL;

    uint128_t cursor = (__uint128_t)0x8000000000000000 << 64 | 0x0;

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

uint128_t ip_get_mask128(uint8_t prefix)
{
        return UINT128_MAX - grpow(2, 128 - prefix) + 1;
}

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

void cidr_to_ip_and_mask128(char *cidr, uint128_t *ip, uint128_t *mask)
{
		char *pref;
		uint8_t prefix;
		*ip = ip_to_integer(cidr, 6, &pref);
		if (*pref == '/')
			prefix = strtoull(++pref, NULL, 10);
		else
			prefix = 128;

		if (mask)
			*mask = ip_get_mask128(prefix);
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

rnode* network_add_ip(patricia_t *tree, patricia_t *tree6, char *s_ip, void *tag) {
    uint8_t ip_version = ip_get_version(s_ip);

    if (ip_version == 4) {
        uint32_t mask;
        uint32_t ip;
        cidr_to_ip_and_mask(s_ip, &ip, &mask);
        return patricia_insert(tree, ip, mask, tag, s_ip);
    } else if (ip_version == 6) {
        uint128_t mask;
        uint128_t ip;
        cidr_to_ip_and_mask128(s_ip, &ip, &mask);
        return patricia_insert128(tree6, ip, mask, tag, s_ip);
    }
    else {
        glog(L_ERROR, "network_add_ip error: unknown ip version '%PRIu8' from addr: %s\n", ip_version, s_ip);
        return NULL;
    }
}

void* network_del_ip(patricia_t *tree, patricia_t *tree6, char *s_ip) {
    uint8_t ip_version = ip_get_version(s_ip);

    if (ip_version == 4) {
        uint32_t mask;
        uint32_t ip;
        cidr_to_ip_and_mask(s_ip, &ip, &mask);
        return patricia_delete(tree, ip, mask, s_ip);
    } else if (ip_version == 6) {
        uint128_t mask;
        uint128_t ip;
        cidr_to_ip_and_mask128(s_ip, &ip, &mask);
        return patricia_delete128(tree6, ip, mask, s_ip);
    }
    else {
        glog(L_ERROR, "network_del_ip error: unknown ip version '%PRIu8' from addr: %s\n", ip_version, s_ip);
        return NULL;
    }
}

//uint32_t fill_networks (patricia_t *tree, char *dir) {
//    struct dirent *ipdirent;
//    DIR *ipdir = opendir(dir);
//    if (!ipdir) {
//        fprintf(stderr, "cannot open dir %s\n", dir);
//        return 0;
//    }
//
//    uint32_t count = 0;
//    char fname[255];
//    size_t dirlen = strlen(dir);
//    strcpy(fname, dir);
//    while ((ipdirent = readdir(ipdir))) {
//        char *countryname = strndup(ipdirent->d_name, strcspn(ipdirent->d_name, "."));
//
//        strcpy(fname+dirlen, ipdirent->d_name);
//        FILE *fd = fopen(fname, "r");
//        if (!fd) {
//            fprintf(stderr, "can't open file %s\n", fname);
//            break;
//        }
//
//        char buf[64];
//        while (fgets(buf, 63, fd)) {
//            buf[strlen(buf) - 1] = '\0';
//            //printf("add %s->%s\n", buf, countryname);
//            if(network_add_ip(tree, buf, countryname))
//                ++count;
//        }
//    }
//
//    return count;
//}

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
