#include "common/log_json.h"

#include "common/log_jansson.h"
#include "main.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <jansson.h>

extern aconf *ac;

#define LOG_CHANNEL_INHERIT (-1)

static int log_json_time_enabled(const log_channel *ch)
{
	if (ch && !ch->is_default && ch->time != LOG_CHANNEL_INHERIT)
		return ch->time;
	return ac && ac->log_time;
}

static const char *log_json_time_format(const log_channel *ch)
{
	if (ch && !ch->is_default && ch->time_format && ch->time_format[0])
		return ch->time_format;
	if (ac && ac->log_time_format && ac->log_time_format[0])
		return ac->log_time_format;
	return "%Y-%m-%d %H:%M:%S";
}

static void log_json_format_date(const log_channel *ch, char *buf, size_t bufsz)
{
	struct timeval tv;
	time_t sec;
	struct tm tm_now;
	const char *fmt;
	size_t n;
	int m;

	if (!buf || bufsz < 2)
		return;

	buf[0] = '\0';

	if (gettimeofday(&tv, NULL) != 0)
		return;

	sec = tv.tv_sec;
#if defined(_WIN32)
	localtime_s(&tm_now, &sec);
#else
	localtime_r(&sec, &tm_now);
#endif

	if (log_json_time_enabled(ch))
		fmt = log_json_time_format(ch);
	else
		fmt = "%Y-%m-%d %H:%M:%S";

	n = strftime(buf, bufsz, fmt, &tm_now);
	if (!n)
	{
		buf[0] = '\0';
		return;
	}

	if (log_json_time_enabled(ch) &&
	    !((ch && !ch->is_default && ch->time_format && ch->time_format[0]) ||
	      (ac && ac->log_time_format && ac->log_time_format[0])))
	{
		if (n + 5 < bufsz)
		{
			m = snprintf(buf + n, bufsz - n, ".%03d", (int)(tv.tv_usec / 1000));
			if (m > 0)
				buf[n + (size_t)m] = '\0';
		}
	}
}

static char *log_json_vformat_message(const char *format, va_list args, size_t *outlen)
{
	char stack[8192];
	va_list args2;
	int msglen;
	char *msg;

	va_copy(args2, args);
	msglen = vsnprintf(stack, sizeof(stack), format, args2);
	va_end(args2);
	if (msglen < 0)
		return NULL;

	if ((size_t)msglen < sizeof(stack))
		msg = strdup(stack);
	else
	{
		msg = malloc((size_t)msglen + 1);
		if (!msg)
			return NULL;

		va_copy(args2, args);
		vsnprintf(msg, (size_t)msglen + 1, format, args2);
		va_end(args2);
	}

	if (msg && outlen)
		*outlen = (size_t)msglen;
	return msg;
}

static size_t log_json_trim_trailing_newline(char *msg, size_t len)
{
	while (len > 0 && msg[len - 1] == '\n')
		len--;
	msg[len] = '\0';
	return len;
}

static json_t *log_json_build_doc(const log_channel *ch, context_arg *carg, const char *message)
{
	json_t *doc;
	char date[128];

	if (!message)
		return NULL;

	doc = json_object();
	if (!doc)
		return NULL;

	json_object_set_new(doc, "message", json_string(message));

	if (carg && carg->key && carg->key[0])
		json_object_set_new(doc, "key", json_string(carg->key));

	log_json_format_date(ch, date, sizeof(date));
	if (date[0])
		json_object_set_new(doc, "date", json_string(date));

	return doc;
}

char *log_json_format_doc_msg(const log_channel *ch, context_arg *carg,
    const char *message, size_t msglen, size_t *outlen)
{
	json_t *doc;
	char *ret;
	char *msg;

	if (!message || !msglen)
		return NULL;

	msg = malloc(msglen + 1);
	if (!msg)
		return NULL;

	memcpy(msg, message, msglen);
	msg[msglen] = '\0';
	msglen = log_json_trim_trailing_newline(msg, msglen);

	doc = log_json_build_doc(ch, carg, msg);
	free(msg);
	if (!doc)
		return NULL;

	ret = log_jansson_dumps_line(doc, outlen);
	json_decref(doc);
	return ret;
}

char *log_json_format_doc(const log_channel *ch, context_arg *carg,
    const char *format, va_list args, size_t *outlen)
{
	char *msg;
	size_t msglen = 0;
	char *ret;

	msg = log_json_vformat_message(format, args, &msglen);
	if (!msg)
		return NULL;

	ret = log_json_format_doc_msg(ch, carg, msg, msglen, outlen);
	free(msg);
	return ret;
}
