#pragma once
#include <stddef.h>

/*
 * OpenBSD strlcpy/strlcat. Linux links a local copy (common/strlcpy.c).
 * Darwin/BSD provide them in libc; still declare here so feature-test
 * macros such as _XOPEN_SOURCE do not hide the prototypes.
 */
size_t strlcpy(char *dst, const char *src, size_t dsize);
size_t strlcat(char *dst, const char *src, size_t dsize);
