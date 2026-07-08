#include "common/log_elastic.h"

#include "common/log_jansson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <jansson.h>

static void log_elastic_iso_timestamp(char *buf, size_t bufsz)
{
	struct timeval tv;
	time_t sec;
	struct tm tm_now;

	if (!buf || bufsz < 21)
		return;

	if (gettimeofday(&tv, NULL) != 0)
	{
		buf[0] = '\0';
		return;
	}

	sec = tv.tv_sec;
#if defined(_WIN32)
	gmtime_s(&tm_now, &sec);
#else
	gmtime_r(&sec, &tm_now);
#endif

	snprintf(buf, bufsz, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
	    tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
	    tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec,
	    (int)(tv.tv_usec / 1000));
}

char *log_elastic_index_name(const log_channel *ch)
{
	const char *tpl = "alligator-%Y.%m.%d";
	char index[256];
	time_t rawtime;
	size_t n;

	if (ch && ch->log_index && ch->log_index[0])
		tpl = ch->log_index;

	time(&rawtime);
	n = strftime(index, sizeof(index), tpl, localtime(&rawtime));
	if (!n)
		return strdup("alligator");

	return strdup(index);
}

static char *log_elastic_vformat_message(const char *format, va_list args, size_t *outlen)
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

static json_t *log_elastic_build_doc(const log_channel *ch, context_arg *carg, int priority,
    const char *message)
{
	json_t *doc = json_object();
	char ts[40];
	const char *level;

	if (!doc || !message)
		return doc;

	log_elastic_iso_timestamp(ts, sizeof(ts));
	level = get_log_level_by_id((uint64_t)priority);

	json_object_set_new(doc, "@timestamp", json_string(ts));
	json_object_set_new(doc, "message", json_string(message));
	if (level)
		json_object_set_new(doc, "log.level", json_string(level));
	json_object_set_new(doc, "service.name", json_string("alligator"));

	if (ch && ch->name && !ch->is_default)
		json_object_set_new(doc, "log.channel", json_string(ch->name));

	if (carg)
	{
		const char *ctx = carg->key ? carg->key : (carg->host[0] ? carg->host : NULL);

		if (ctx)
			json_object_set_new(doc, "service.context", json_string(ctx));
	}

	return doc;
}

char *log_elastic_format_doc_msg(const log_channel *ch, context_arg *carg, int priority,
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

	doc = log_elastic_build_doc(ch, carg, priority, msg);
	free(msg);
	if (!doc)
		return NULL;

	ret = log_jansson_dumps_line(doc, outlen);
	json_decref(doc);
	return ret;
}

char *log_elastic_format_doc(const log_channel *ch, context_arg *carg, int priority,
    const char *format, va_list args, size_t *outlen)
{
	char *msg;
	size_t msglen = 0;
	char *ret;

	msg = log_elastic_vformat_message(format, args, &msglen);
	if (!msg)
		return NULL;

	ret = log_elastic_format_doc_msg(ch, carg, priority, msg, msglen, outlen);
	free(msg);
	return ret;
}

char *log_elastic_format_bulk(const log_channel *ch, context_arg *carg, int priority,
    const char *format, va_list args, size_t *outlen)
{
	char *msg;
	size_t msglen = 0;
	char *index;
	json_t *doc;
	json_t *action;
	json_t *meta;
	char *action_json;
	char *doc_json;
	char *ret;
	size_t action_len;
	size_t doc_len;
	size_t total;

	msg = log_elastic_vformat_message(format, args, &msglen);
	if (!msg)
		return NULL;

	(void)msglen;
	index = log_elastic_index_name(ch);
	doc = log_elastic_build_doc(ch, carg, priority, msg);
	free(msg);
	if (!doc || !index)
	{
		free(index);
		json_decref(doc);
		return NULL;
	}

	action = json_object();
	meta = json_object();
	json_object_set_new(meta, "_index", json_string(index));
	json_object_set_new(action, "index", meta);
	free(index);

	action_json = log_jansson_dumps_compact(action, &action_len);
	doc_json = log_jansson_dumps_compact(doc, &doc_len);
	json_decref(action);
	json_decref(doc);

	if (!action_json || !doc_json)
	{
		free(action_json);
		free(doc_json);
		return NULL;
	}

	total = action_len + 1 + doc_len + 1;
	ret = malloc(total + 1);
	if (!ret)
	{
		free(action_json);
		free(doc_json);
		return NULL;
	}

	memcpy(ret, action_json, action_len);
	ret[action_len] = '\n';
	memcpy(ret + action_len + 1, doc_json, doc_len);
	ret[action_len + 1 + doc_len] = '\n';
	ret[total] = '\0';
	free(action_json);
	free(doc_json);

	if (outlen)
		*outlen = total;
	return ret;
}
