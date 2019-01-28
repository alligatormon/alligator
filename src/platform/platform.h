#pragma once
#ifdef __linux__
#include <bsd/string.h>
#endif
#ifdef _WIN32
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif
