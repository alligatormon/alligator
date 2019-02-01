#include <string.h>
#include <stdlib.h>
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
	char *ret = malloc(sz+1);
	strlcpy(ret, str, sz+1);
	return ret;
}
