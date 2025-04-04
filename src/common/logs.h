#pragma once
#include <inttypes.h>
#include <stddef.h>
#include <stdarg.h>
char* get_log_level_by_id(uint64_t id);
uint64_t get_log_level_by_name(const char *val, size_t len);
void log_init();
void log_default();
void wrlog(int level, int priority, const char *format, va_list args);
void glog(int priority, const char *format, ...);

enum {
	FORM_SIMPLE,
	FORM_SYSLOG,
};

enum {
	L_OFF,
	L_FATAL,
	L_ERROR,
	L_WARN,
	L_INFO,
	L_DEBUG,
	L_TRACE,
};

