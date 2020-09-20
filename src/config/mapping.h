#pragma once
#include <jansson.h>
#define MAPPING_MATCH_GLOB 0
#define MAPPING_MATCH_PCRE 1
#define MAPPING_MATCH_GROK 2
mapping_metric* json_mapping_parser(json_t *mapping);
