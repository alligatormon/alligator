#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <common/selector.h>
#include <common/logs.h>
#include <common/log_tcp.h>
#include <common/log_http.h>
#include <common/log_elastic.h>
#include <common/log_json.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <jansson.h>
#include "events/context_arg.h"
#include "main.h"

extern aconf *ac;

static log_channel log_default_channel;
#define LOG_CHANNEL_INHERIT (-1)

static int log_channel_time_enabled(log_channel *ch)
{
	if (ch && !ch->is_default && ch->time != LOG_CHANNEL_INHERIT)
		return ch->time;
	return ac && ac->log_time;
}

static const char *log_channel_time_format(log_channel *ch)
{
	if (ch && !ch->is_default && ch->time_format && ch->time_format[0])
		return ch->time_format;
	if (ac && ac->log_time_format && ac->log_time_format[0])
		return ac->log_time_format;
	return "%Y-%m-%d %H:%M:%S";
}

static void log_channel_write(log_channel *ch, const char *data, size_t len)
{
	ssize_t n;

	if (!ch || !data || !len || ch->socket < 0)
		return;

	if (ch->dest_kind == LOG_DEST_UDP)
		n = send(ch->socket, data, len, MSG_NOSIGNAL);
	else
		n = write(ch->socket, data, len);

	(void)n;
}

static size_t log_append_timestamp_prefix(log_channel *ch, char *buf, size_t bufsz, size_t off)
{
	struct timeval tv;
	time_t sec;
	struct tm tm_now;
	char ts[128];
	const char *fmt;
	int n;

	if (!ch || !log_channel_time_enabled(ch) || off >= bufsz)
		return off;

	if (gettimeofday(&tv, NULL) != 0)
		return off;

	sec = tv.tv_sec;
#if defined(_WIN32)
	localtime_s(&tm_now, &sec);
#else
	localtime_r(&sec, &tm_now);
#endif

	fmt = log_channel_time_format(ch);
	if (!strftime(ts, sizeof(ts), fmt, &tm_now))
		return off;

	if ((ch && !ch->is_default && ch->time_format && ch->time_format[0]) ||
	    (ac && ac->log_time_format && ac->log_time_format[0]))
		n = snprintf(buf + off, bufsz - off, "%s ", ts);
	else
		n = snprintf(buf + off, bufsz - off, "%s.%03d ", ts, (int)(tv.tv_usec / 1000));

	if (n > 0 && (size_t)n < bufsz - off)
		off += (size_t)n;

	return off;
}

static size_t log_append_context_prefix(log_channel *ch, context_arg *carg, char *buf, size_t bufsz, size_t off)
{
	const char *ctx;
	int n;

	if (!ch || !carg || off >= bufsz)
		return off;

	ctx = carg->key ? carg->key : (carg->host[0] ? carg->host : NULL);
	if (!ctx)
		return off;

	if (ch->name && !ch->is_default)
		n = snprintf(buf + off, bufsz - off, "[%s/%s] ", ch->name, ctx);
	else
		n = snprintf(buf + off, bufsz - off, "[%s] ", ctx);

	if (n > 0 && (size_t)n < bufsz - off)
		off += (size_t)n;

	return off;
}

static char *log_format_line_alloc(log_channel *ch, context_arg *carg, const char *format, va_list args, size_t *outlen)
{
	char stack[8192];
	va_list args2;
	int msglen;
	size_t off = 0;
	char *buf;
	size_t cap = 8192;

	va_copy(args2, args);
	msglen = vsnprintf(stack, sizeof(stack), format, args2);
	va_end(args2);
	if (msglen < 0)
		return NULL;

	buf = malloc(cap);
	if (!buf)
		return NULL;

	off = log_append_timestamp_prefix(ch, buf, cap, off);
	off = log_append_context_prefix(ch, carg, buf, cap, off);

	if ((size_t)msglen >= cap - off)
	{
		cap = off + (size_t)msglen + 1;
		{
			char *grown = realloc(buf, cap);
			if (!grown)
			{
				free(buf);
				return NULL;
			}
			buf = grown;
		}
	}

	memcpy(buf + off, stack, (size_t)msglen);
	off += (size_t)msglen;

	if (outlen)
		*outlen = off;

	return buf;
}

static void log_channel_close_socket(log_channel *ch)
{
	if (!ch)
		return;

	if (ch->dest_kind == LOG_DEST_TCP)
	{
		log_tcp_sink_close(ch);
		return;
	}

	if (ch->dest_kind == LOG_DEST_HTTP)
	{
		log_http_sink_close(ch);
		return;
	}

	if (ch->socket <= 2)
		return;

	close(ch->socket);
	ch->socket = fileno(stdout);
	ch->dest_kind = LOG_DEST_FD;
	if (ch->host) {
		free(ch->host);
		ch->host = NULL;
	}
}

static int log_channel_open_dest(log_channel *ch, char *dest)
{
	if (!ch)
		return -1;

	log_channel_close_socket(ch);

	if (!dest) {
		ch->socket = fileno(stdout);
		ch->dest_kind = LOG_DEST_FD;
	} else if (!strncmp(dest, "stdout", 6)) {
		ch->socket = fileno(stdout);
		ch->dest_kind = LOG_DEST_FD;
	} else if (!strncmp(dest, "stderr", 6)) {
		ch->socket = fileno(stderr);
		ch->dest_kind = LOG_DEST_FD;
	} else if (!strncmp(dest, "udp://", 6)) {
		host_aggregator_info *hi = parse_url(dest, strlen(dest));
		if (!hi) {
			ch->socket = fileno(stdout);
			ch->dest_kind = LOG_DEST_FD;
			return 0;
		}

		if (ch->host)
			free(ch->host);

		ch->host = strdup(hi->host);
		ch->port = (int)strtoull(hi->port, NULL, 10);
		bzero(&ch->soaddr, sizeof(ch->soaddr));
		ch->soaddr.sin_addr.s_addr = inet_addr(ch->host);
		ch->soaddr.sin_port = htons(ch->port);
		ch->soaddr.sin_family = AF_INET;
		ch->socket = socket(AF_INET, SOCK_DGRAM, 0);
		ch->dest_kind = LOG_DEST_UDP;

		if (connect(ch->socket, (struct sockaddr *)&ch->soaddr, sizeof(ch->soaddr)) < 0) {
			printf("log connect failed\n");
			ch->socket = fileno(stdout);
			ch->dest_kind = LOG_DEST_FD;
		}

		url_free(hi);
	} else if (!strncmp(dest, "tcp://", 6)) {
		host_aggregator_info *hi = parse_url(dest, strlen(dest));
		if (!hi) {
			ch->socket = fileno(stdout);
			ch->dest_kind = LOG_DEST_FD;
			return 0;
		}

		if (ch->host)
			free(ch->host);

		ch->host = strdup(hi->host);
		ch->port = (int)strtoull(hi->port, NULL, 10);
		log_tcp_sink_open(ch, ch->host, ch->port);
		url_free(hi);
	} else if (!strncmp(dest, "http://", 7)) {
		host_aggregator_info *hi = parse_url(dest, strlen(dest));
		if (!hi) {
			ch->socket = fileno(stdout);
			ch->dest_kind = LOG_DEST_FD;
			return 0;
		}

		if (ch->host)
			free(ch->host);

		ch->host = strdup(hi->host);
		ch->port = (int)strtoull(hi->port, NULL, 10);
		if (ch->log_format == LOG_FORMAT_INHERIT)
			ch->log_format = LOG_FORMAT_ELASTIC;
		log_http_sink_open(ch, ch->host, ch->port, hi->query);
		url_free(hi);
	} else if (!strncmp(dest, "file://", 7)) {
		host_aggregator_info *hi = parse_url(dest, strlen(dest));
		if (!hi) {
			ch->socket = fileno(stdout);
			ch->dest_kind = LOG_DEST_FD;
			return 0;
		}

		ch->socket = open(hi->host, O_WRONLY|O_APPEND|O_CREAT|O_NONBLOCK, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
		ch->dest_kind = LOG_DEST_FD;
		url_free(hi);
	} else {
		ch->socket = fileno(stdout);
		ch->dest_kind = LOG_DEST_FD;
	}

	return 0;
}

int log_channel_compare(const void *arg, const void *obj)
{
	const char *s1 = arg;
	const char *s2 = ((log_channel *)obj)->name;
	return strcmp(s1, s2);
}

void log_channels_init(void)
{
	log_default_channel.name = (char *)"default";
	log_default_channel.is_default = 1;
	log_default_channel.form = LOG_CHANNEL_INHERIT;
	log_default_channel.time = LOG_CHANNEL_INHERIT;
	log_default_channel.socket = fileno(stdout);

	if (!ac->log_channels) {
		ac->log_channels = calloc(1, sizeof(alligator_ht));
		alligator_ht_init(ac->log_channels);
	}
}

static void log_channel_free_foreach(void *funcarg, void *arg)
{
	(void)funcarg;
	log_channel *ch = arg;

	if (!ch || ch->is_default)
		return;

	log_channel_close_socket(ch);
	free(ch->name);
	free(ch->dest);
	free(ch->time_format);
	free(ch->log_index);
	free(ch);
}

void log_channels_free(void)
{
	if (!ac || !ac->log_channels)
		return;

	log_tcp_sink_close(log_channel_default());
	log_http_sink_close(log_channel_default());
	alligator_ht_foreach_arg(ac->log_channels, log_channel_free_foreach, NULL);
	alligator_ht_done(ac->log_channels);
	free(ac->log_channels);
	ac->log_channels = NULL;
}

log_channel *log_channel_default(void)
{
	log_default_channel.dest = ac ? ac->log_dest : NULL;
	log_default_channel.form = ac ? ac->log_form : FORM_SIMPLE;
	log_default_channel.time = ac ? ac->log_time : 0;
	log_default_channel.time_format = ac ? ac->log_time_format : NULL;
	log_default_channel.socket = ac ? ac->log_socket : fileno(stdout);
	return &log_default_channel;
}

log_channel *log_channel_get(const char *name)
{
	log_channel *ch;

	if (!name || !name[0] || !strcmp(name, "default"))
		return log_channel_default();

	if (!ac || !ac->log_channels)
		return log_channel_default();

	ch = alligator_ht_search(ac->log_channels, log_channel_compare, (char *)name,
	    tommy_strhash_u32(0, name));
	if (!ch)
		return log_channel_default();

	return ch;
}

log_channel *log_channel_upsert(const char *name, const char *dest, int form, int time,
    const char *time_format, int log_format, const char *log_index)
{
	log_channel *ch;
	uint32_t key_hash;

	if (!name || !name[0] || !dest || !dest[0])
		return NULL;

	if (!strcmp(name, "default")) {
		if (ac->log_dest)
			free(ac->log_dest);
		ac->log_dest = strdup(dest);
		if (form != LOG_CHANNEL_INHERIT)
			ac->log_form = form;
		if (time != LOG_CHANNEL_INHERIT)
			ac->log_time = time;
		if (time_format) {
			if (ac->log_time_format)
				free(ac->log_time_format);
			ac->log_time_format = strdup(time_format);
		}
		log_channel_open(log_channel_default());
		return log_channel_default();
	}

	log_channels_init();
	key_hash = tommy_strhash_u32(0, name);
	ch = alligator_ht_search(ac->log_channels, log_channel_compare, (char *)name, key_hash);
	if (!ch) {
		ch = calloc(1, sizeof(*ch));
		ch->name = strdup(name);
		ch->form = LOG_CHANNEL_INHERIT;
		ch->time = LOG_CHANNEL_INHERIT;
		ch->log_format = LOG_FORMAT_INHERIT;
		alligator_ht_insert(ac->log_channels, &(ch->node), ch, key_hash);
	}

	if (ch->dest)
		free(ch->dest);
	ch->dest = strdup(dest);

	if (form != LOG_CHANNEL_INHERIT)
		ch->form = form;
	if (time != LOG_CHANNEL_INHERIT)
		ch->time = time;
	if (time_format) {
		if (ch->time_format)
			free(ch->time_format);
		ch->time_format = strdup(time_format);
	}
	if (log_format != LOG_FORMAT_INHERIT)
		ch->log_format = (int8_t)log_format;
	else
		ch->log_format = LOG_FORMAT_INHERIT;
	if (log_index) {
		if (ch->log_index)
			free(ch->log_index);
		ch->log_index = strdup(log_index);
	}

	log_channel_open(ch);
	return ch;
}

int log_channel_format_elastic(const log_channel *ch)
{
	return ch && ch->log_format == LOG_FORMAT_ELASTIC;
}

int log_channel_format_json(const log_channel *ch)
{
	return ch && ch->log_format == LOG_FORMAT_JSON;
}

static int log_channel_parse_format(json_t *value)
{
	const char *fmt;

	if (!value || json_typeof(value) != JSON_STRING)
		return LOG_FORMAT_INHERIT;

	fmt = json_string_value(value);
	if (!fmt)
		return LOG_FORMAT_INHERIT;
	if (!strcmp(fmt, "plain"))
		return LOG_FORMAT_PLAIN;
	if (!strcmp(fmt, "json"))
		return LOG_FORMAT_JSON;
	if (!strcmp(fmt, "elastic") || !strcmp(fmt, "elasticsearch") || !strcmp(fmt, "ecs"))
		return LOG_FORMAT_ELASTIC;
	return LOG_FORMAT_INHERIT;
}

void log_channel_open(log_channel *ch)
{
	if (!ch)
		return;

	if (ch->is_default) {
		if (ac->log_host)
			free(ac->log_host);
		ac->log_host = NULL;
		log_channel_open_dest(ch, ac->log_dest);
		ac->log_socket = ch->socket;
		ac->logsoaddr = ch->soaddr;
		ac->log_port = ch->port;
		if (ch->host) {
			ac->log_host = strdup(ch->host);
			free(ch->host);
			ch->host = NULL;
		}
		return;
	}

	log_channel_open_dest(ch, ch->dest);
}

static void log_channels_reopen_foreach(void *funcarg, void *arg)
{
	(void)funcarg;
	log_channel_open(arg);
}

void log_channels_reopen(void)
{
	log_channel_open(log_channel_default());
	if (ac && ac->log_channels)
		alligator_ht_foreach_arg(ac->log_channels, log_channels_reopen_foreach, NULL);
}

static int log_channel_parse_bool(json_t *value)
{
	if (json_is_true(value))
		return 1;
	if (json_is_false(value))
		return 0;
	if (json_typeof(value) == JSON_INTEGER)
		return json_integer_value(value) != 0;
	if (json_typeof(value) == JSON_STRING) {
		const char *v = json_string_value(value);
		return !strcasecmp(v, "on") || !strcasecmp(v, "true") ||
		    !strcasecmp(v, "yes") || !strcmp(v, "1");
	}
	return LOG_CHANNEL_INHERIT;
}

static int log_channel_parse_form(json_t *value)
{
	const char *form;

	if (!value || json_typeof(value) != JSON_STRING)
		return LOG_CHANNEL_INHERIT;

	form = json_string_value(value);
	if (!strcmp(form, "syslog"))
		return FORM_SYSLOG;
	return FORM_SIMPLE;
}

void log_channels_config_json(json_t *value)
{
	size_t i;
	size_t n;

	if (!value || !json_is_array(value))
		return;

	n = json_array_size(value);
	for (i = 0; i < n; i++) {
		json_t *item = json_array_get(value, i);
		json_t *jname = json_object_get(item, "name");
		json_t *jdest = json_object_get(item, "dest");
		const char *name;
		const char *dest;
		int form = LOG_CHANNEL_INHERIT;
		int time = LOG_CHANNEL_INHERIT;
		int log_format = LOG_FORMAT_INHERIT;
		const char *time_format = NULL;
		const char *log_index = NULL;
		json_t *jform;
		json_t *jtime;
		json_t *jtime_format;
		json_t *jformat;
		json_t *jindex;

		if (!jdest)
			jdest = json_object_get(item, "log_dest");
		if (!jname || !jdest)
			continue;

		name = json_string_value(jname);
		dest = json_string_value(jdest);
		if (!name || !dest)
			continue;

		jform = json_object_get(item, "log_form");
		jtime = json_object_get(item, "log_time");
		jtime_format = json_object_get(item, "log_time_format");
		jformat = json_object_get(item, "log_format");
		jindex = json_object_get(item, "log_index");
		form = log_channel_parse_form(jform);
		time = log_channel_parse_bool(jtime);
		if (jtime_format && json_typeof(jtime_format) == JSON_STRING)
			time_format = json_string_value(jtime_format);
		log_format = log_channel_parse_format(jformat);
		if (jindex && json_typeof(jindex) == JSON_STRING)
			log_index = json_string_value(jindex);

		log_channel_upsert(name, dest, form, time, time_format, log_format, log_index);
	}
}

uint64_t get_log_level_by_name(const char *val, size_t len) {
	uint64_t level = L_OFF;
	if (sisdigit(val)) {
		level = strtoull(val, NULL, 10);
	} else {
		if (!strncasecmp(val, "off", 3)) {
			level = L_OFF;
		} else if (!strncasecmp(val, "fatal", 5)) {
			level = L_FATAL;
		} else if (!strncasecmp(val, "error", 5)) {
			level = L_ERROR;
		} else if (!strncasecmp(val, "warn", 4)) {
			level = L_WARN;
		} else if (!strncasecmp(val, "info", 4)) {
			level = L_INFO;
		} else if (!strncasecmp(val, "debug", 5)) {
			level = L_DEBUG;
		} else if (!strncasecmp(val, "trace", 5)) {
			level = L_TRACE;
		}
	}

	return level;
}

char* get_log_level_by_id(uint64_t id) {
	char *ret;
	if (id == L_OFF) {
		ret = "off";
	} else if (id == L_FATAL) {
		ret = "fatal";
	} else if (id == L_ERROR) {
		ret = "error";
	} else if (id == L_WARN) {
		ret = "warn";
	} else if (id == L_INFO) {
		ret = "info";
	} else if (id == L_DEBUG) {
		ret = "debug";
	} else if (id == L_TRACE) {
		ret = "trace";
	} else {
		ret = NULL;
	}

	return ret;
}

void log_default() {
	ac->log_socket = fileno(stdout);
	log_channels_init();
	log_default_channel.socket = ac->log_socket;
}

void log_init() {
	log_channels_reopen();
}

void wrlog(log_channel *ch, context_arg *carg, int level, int priority, const char *format, va_list args)
{
	if (level < priority)
		return;

	if (!ch)
		ch = log_channel_for_context(carg);

	if (log_channel_is_tcp(ch) || log_channel_is_http(ch))
	{
		char *payload = NULL;
		size_t len = 0;
		va_list args_copy;

		if (log_channel_format_elastic(ch))
		{
			va_copy(args_copy, args);
			if (log_channel_is_http(ch))
				payload = log_elastic_format_bulk(ch, carg, priority, format, args_copy, &len);
			else
				payload = log_elastic_format_doc(ch, carg, priority, format, args_copy, &len);
			va_end(args_copy);
		}
		else if (log_channel_format_json(ch))
		{
			va_copy(args_copy, args);
			payload = log_json_format_doc(ch, carg, format, args_copy, &len);
			va_end(args_copy);
		}
		else
			payload = log_format_line_alloc(ch, carg, format, args, &len);

		if (payload && len)
		{
			if (log_channel_is_http(ch))
				log_http_sink_write(ch, payload, len);
			else
				log_tcp_sink_write(ch, payload, len);
		}
		free(payload);
		return;
	}

	{
		char *payload = NULL;
		size_t len = 0;
		va_list args_copy;

		if (log_channel_format_json(ch))
		{
			va_copy(args_copy, args);
			payload = log_json_format_doc(ch, carg, format, args_copy, &len);
			va_end(args_copy);
		}
		else
			payload = log_format_line_alloc(ch, carg, format, args, &len);

		if (payload && len)
			log_channel_write(ch, payload, len);
		free(payload);
	}
}

log_channel *log_channel_for_context(context_arg *carg)
{
	if (carg && carg->log_ch)
		return carg->log_ch;
	return log_channel_default();
}

void glog(int priority, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	wrlog(NULL, NULL, ac->log_level, priority, format, args);
	va_end(args);
}
