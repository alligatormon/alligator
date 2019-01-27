#pragma once
#ifdef __linux__
#include <bsd/string.h>
#endif
#ifdef _WIN32
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
#endif
