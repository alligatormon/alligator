#pragma once
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#define IPACCESS_DENY 0
#define IPACCESS_ALLOW 1

__extension__ typedef unsigned __int128 uint128_t;
//typedef uint64_t uint128_t;

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
