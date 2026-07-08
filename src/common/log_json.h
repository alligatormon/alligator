#pragma once

#include "common/logs.h"
#include "events/context_arg.h"

#include <stddef.h>
#include <stdarg.h>

char *log_json_format_doc_msg(const log_channel *ch, context_arg *carg,
    const char *message, size_t msglen, size_t *outlen);
char *log_json_format_doc(const log_channel *ch, context_arg *carg,
    const char *format, va_list args, size_t *outlen);
