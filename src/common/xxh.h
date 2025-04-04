#include <stdint.h>
#include "xxhash.h"
uint64_t xxh3_run(const char* string, uint32_t length, uint32_t initial);
uint64_t xxh3_get(const char* string);
