#pragma once
#include <inttypes.h>
#include <stddef.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <jansson.h>
#include "dstructures/tommy.h"

struct context_arg;

enum log_dest_kind {
	LOG_DEST_FD = 0,
	LOG_DEST_UDP,
	LOG_DEST_TCP,
	LOG_DEST_HTTP,
};

enum log_format_kind {
	LOG_FORMAT_INHERIT = -1,
	LOG_FORMAT_PLAIN = 0,
	LOG_FORMAT_ELASTIC,
	LOG_FORMAT_JSON,
};

typedef struct log_channel {
	char *name;
	char *dest;
	int socket;
	int form;
	int time;
	char *time_format;
	char *log_index;
	char *host;
	int port;
	struct sockaddr_in soaddr;
	uint8_t dest_kind;
	int8_t log_format;
	uint8_t is_default;
	void *tcp_sink;
	void *http_sink;
	tommy_node node;
} log_channel;

char* get_log_level_by_id(uint64_t id);
uint64_t get_log_level_by_name(const char *val, size_t len);
void log_channels_init(void);
void log_channels_free(void);
log_channel *log_channel_default(void);
log_channel *log_channel_get(const char *name);
log_channel *log_channel_for_context(struct context_arg *carg);
log_channel *log_channel_upsert(const char *name, const char *dest, int form, int time,
    const char *time_format, int log_format, const char *log_index);
int log_channel_format_elastic(const log_channel *ch);
int log_channel_format_json(const log_channel *ch);
void log_channel_open(log_channel *ch);
void log_channels_reopen(void);
void log_init();
void log_default();
void wrlog(log_channel *ch, struct context_arg *carg, int level, int priority, const char *format, va_list args);
void glog(int priority, const char *format, ...);
void log_channels_config_json(json_t *value);

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
