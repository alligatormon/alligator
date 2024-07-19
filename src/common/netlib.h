#pragma once
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#include <common/patricia.h>
#define IPACCESS_DENY 0
#define IPACCESS_ALLOW 1

//char* integer_to_ip(uint128_t ipaddr, uint8_t ip_version);
uint8_t ip_check_access(patricia_t* tree, patricia_t* tree6, char *ip);
void network_range_push(patricia_t** tree, patricia_t** tree6, char *cidr, uint8_t action);
void network_range_delete(patricia_t* tree, patricia_t* tree6, char *cidr);
