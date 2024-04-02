#pragma once
#include <jansson.h>
typedef struct puppeteer_node {
	string *url;
	char *value;
	
	tommy_node node;
} puppeteer_node;

void puppeteer_delete(json_t *root);
void puppeteer_insert(json_t *root);
void puppeteer_done();
void puppeteer_generator();
