#pragma once
#include "dstructures/tommyds/tommyds/tommy.h"
typedef struct https_ssl_check_node
{
	char *domainname;
	size_t len;
	tommy_node node;
} https_ssl_check_node;
void check_https_cert(const char* hostname);
void https_ssl_check_push(char *hostname);
