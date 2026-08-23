#pragma once
#include <stddef.h>

/*
 * OpenBSD strlcpy/strlcat — provided locally on Linux when libc lacks them.
 * Darwin/BSD expose these via <string.h>.
 */
#if defined(__linux__)
size_t strlcpy(char *dst, const char *src, size_t dsize);
size_t strlcat(char *dst, const char *src, size_t dsize);
#endif
