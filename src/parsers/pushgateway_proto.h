#pragma once
#include <stddef.h>
#include <stdint.h>
#include "common/selector.h"

int pushgateway_proto_delimited_to_text(const char *body, size_t size, string *out,
	uint64_t *families, uint64_t *series, uint64_t *rejected);
