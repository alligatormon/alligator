#pragma once
#ifdef __linux__
#include <bsd/string.h>
#endif
#ifdef _WIN32
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#include <inttypes.h>
//#include <stdlib.h>
#define _int64_t long long int
#define _int64 long long int
#define __int64 long long int
#define __int32 int
size_t
strlcpy(char *dst, const char *src, size_t siz)
{
       char *d = dst;
       const char *s = src;
       size_t n = siz;
       if (n != 0) {
               while (--n != 0) {
                       if ((*d++ = *s++) == '\0')
                               break;
               }
       }
       if (n == 0) {
               if (siz != 0)
                       *d = '\0';
               while (*s++)
                       ;
       }
       return(s - src - 1);
}
char* strndup(char *str, size_t sz)
{
	char *ret = malloc(sz);
	strlcpy(ret, str, sz);
	return ret;
}
#endif
