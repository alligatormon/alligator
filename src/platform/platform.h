#pragma once
#ifdef __linux__
#include <bsd/string.h>
#endif
#ifdef _WIN32
#define SIGUSR1 0
#define SIGUSR2 0
#define SIGQUIT 0
#define SIGTRAP 0
#define SIGPIPE 0
#define STDIN_FILENO 0
#define STDOUT_FILENO 0
#define STDERR_FILENO 0
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
