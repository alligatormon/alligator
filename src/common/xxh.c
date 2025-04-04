#include "common/xxh.h"
#include <string.h>
 
uint64_t xxh3_run(const char* string, uint32_t length, uint32_t initial)
{
    return XXH3_64bits(string, length);
}


uint64_t xxh3_get(const char* string)
{
    size_t length = (string == NULL) ? 0 : strlen(string);
    return XXH3_64bits(string, length);
}

