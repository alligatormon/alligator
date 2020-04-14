#pragma once
#include "events/context_arg.h"
int json_parser_entry(char *line, int argc, char **argv, context_arg *carg);
uint8_t json_check(const char *text);
int8_t json_validator(char *data, size_t size);
