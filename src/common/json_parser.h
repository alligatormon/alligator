#pragma once
#include "events/context_arg.h"
void json_parser_entry(char *line, int argc, char **argv, char *name, context_arg *carg);
uint8_t json_check(const char *text);
int8_t json_validator(context_arg *carg, char *data, size_t size);
