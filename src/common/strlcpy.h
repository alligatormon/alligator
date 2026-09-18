#pragma once
#include <stddef.h>

/*
 * OpenBSD strlcpy/strlcat.
 * Linux: local copy in common/strlcpy.c.
 * FreeBSD: libc, but -std=c11 / _XOPEN_SOURCE hide the <string.h> prototypes.
 * Darwin: fortify macros in <secure/_string.h> — do not redeclare.
 */
#if !defined(__APPLE__)
size_t strlcpy(char *dst, const char *src, size_t dsize);
size_t strlcat(char *dst, const char *src, size_t dsize);
#endif
