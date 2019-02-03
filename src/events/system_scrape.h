#pragma once
#include "dstructures/tommy.h"
typedef struct system_scrape_info
{
	char *key;
	void *parser_handler;

	tommy_node node;
} system_scrape_info;
