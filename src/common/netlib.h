#pragma once
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#include <common/patricia.h>
#define IPACCESS_DENY 0
#define IPACCESS_ALLOW 1

typedef struct network_range_node
{
	uint128_t start;
	uint128_t end;
	uint8_t action;
} network_range_node;

typedef struct network_range
{
	network_range_node *nr_node;
	uint64_t max;
	uint64_t cur;
} network_range;

void cidr_to_network_range(network_range_node *nr, char *cidr);
char* integer_to_ip(uint128_t ipaddr, uint8_t ip_version);
network_range* network_range_duplicate(network_range *nr);
void network_range_free(network_range *nr);
uint8_t ip_check_access(network_range *nr, patricia_t *tree, char *ip);
void network_range_push(network_range *nr, patricia_t **tree, char *cidr, uint8_t action);
void network_range_delete(network_range *nr, char *cidr);
