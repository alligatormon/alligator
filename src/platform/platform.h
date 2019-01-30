#pragma once
#ifdef __linux__
#include <bsd/string.h>
#endif
#ifdef _WIN32
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#include <inttypes.h>
#define _int64_t long long int
#define _int64 long long int
#define __int64 long long int
#define __int32 int
char* strndup(char *str, size_t sz)
{
	char *ret = malloc(sz);
	strlcpy(ret, str, sz);
	return ret;
}
#endif
