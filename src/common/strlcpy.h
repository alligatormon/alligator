#pragma once
#include <stddef.h>

/*
 * OpenBSD strlcpy/strlcat — local copy on Linux (common/strlcpy.c).
 * Darwin fortifies these as macros in <secure/_string.h>; do not
 * redeclare them there. FreeBSD exposes them via <string.h>.
 */
#if defined(__linux__)
size_t strlcpy(char *dst, const char *src, size_t dsize);
size_t strlcat(char *dst, const char *src, size_t dsize);
#endif
