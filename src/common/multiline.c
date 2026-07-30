#include "common/multiline.h"
#include "common/pcre_parser.h"
#include <pcre.h>
#include <stdlib.h>
#include <string.h>

struct alligator_multiline {
	regex_match *start_re;
	regex_match *cond_re;
	alligator_ml_mode mode;
	char *buf;
	size_t len;
	size_t cap;
	int have;
	/* continue_past: after a non-matching condition line we already
	 * appended+emitted; unused otherwise. */
};

int alligator_ml_mode_from_str(const char *s, alligator_ml_mode *out)
{
	if (!s || !out)
		return -1;
	if (!strcmp(s, "continue_through")) {
		*out = ALLIGATOR_ML_CONTINUE_THROUGH;
		return 0;
	}
	if (!strcmp(s, "continue_past")) {
		*out = ALLIGATOR_ML_CONTINUE_PAST;
		return 0;
	}
	if (!strcmp(s, "halt_before")) {
		*out = ALLIGATOR_ML_HALT_BEFORE;
		return 0;
	}
	if (!strcmp(s, "halt_with")) {
		*out = ALLIGATOR_ML_HALT_WITH;
		return 0;
	}
	return -1;
}

const char *alligator_ml_mode_to_str(alligator_ml_mode mode)
{
	switch (mode) {
	case ALLIGATOR_ML_CONTINUE_THROUGH: return "continue_through";
	case ALLIGATOR_ML_CONTINUE_PAST: return "continue_past";
	case ALLIGATOR_ML_HALT_BEFORE: return "halt_before";
	case ALLIGATOR_ML_HALT_WITH: return "halt_with";
	}
	return "continue_through";
}

static int ml_match(regex_match *re, const char *line, size_t len)
{
	if (!re || !re->regex_compiled)
		return 0;
	int ovector[30];
	int rc = pcre_exec(re->regex_compiled, re->pcreExtra, line, (int)len,
			   0, 0, ovector, 30);
	return rc >= 0;
}

alligator_multiline *alligator_multiline_new(const char *start_pattern,
					     const char *condition_pattern,
					     alligator_ml_mode mode,
					     char **err)
{
	if (!start_pattern || !*start_pattern ||
	    !condition_pattern || !*condition_pattern) {
		if (err)
			*err = strdup("multiline requires start_pattern and condition_pattern");
		return NULL;
	}
	alligator_multiline *ml = calloc(1, sizeof(*ml));
	if (!ml)
		return NULL;
	ml->mode = mode;
	ml->start_re = regex_match_init((char *)start_pattern, NULL);
	if (!ml->start_re) {
		if (err)
			*err = strdup("multiline: invalid start_pattern");
		free(ml);
		return NULL;
	}
	ml->cond_re = regex_match_init((char *)condition_pattern, NULL);
	if (!ml->cond_re) {
		if (err)
			*err = strdup("multiline: invalid condition_pattern");
		regex_match_free(ml->start_re);
		free(ml);
		return NULL;
	}
	return ml;
}

void alligator_multiline_free(alligator_multiline *ml)
{
	if (!ml)
		return;
	if (ml->start_re)
		regex_match_free(ml->start_re);
	if (ml->cond_re)
		regex_match_free(ml->cond_re);
	free(ml->buf);
	free(ml);
}

static void ml_reserve(alligator_multiline *ml, size_t extra)
{
	if (ml->len + extra + 1 <= ml->cap)
		return;
	size_t cap = ml->cap ? ml->cap * 2 : 256;
	while (cap < ml->len + extra + 1)
		cap *= 2;
	ml->buf = realloc(ml->buf, cap);
	ml->cap = cap;
}

static void ml_append(alligator_multiline *ml, const char *line, size_t len)
{
	ml_reserve(ml, len + 1);
	if (ml->have && ml->len)
		ml->buf[ml->len++] = '\n';
	memcpy(ml->buf + ml->len, line, len);
	ml->len += len;
	ml->buf[ml->len] = '\0';
	ml->have = 1;
}

static void ml_emit(alligator_multiline *ml, alligator_ml_cb cb, void *ud)
{
	if (!ml->have || !cb)
		return;
	cb(ud, ml->buf ? ml->buf : "", ml->len);
	ml->len = 0;
	ml->have = 0;
	if (ml->buf)
		ml->buf[0] = '\0';
}

void alligator_multiline_feed(alligator_multiline *ml,
			      const char *line, size_t len,
			      alligator_ml_cb cb, void *ud)
{
	if (!ml)
		return;
	int is_start = ml_match(ml->start_re, line, len);
	int is_cond = ml_match(ml->cond_re, line, len);

	if (!ml->have) {
		/* Only begin a new message on start_pattern (Vector).
		 * Orphan non-start lines are emitted as single-line records. */
		if (is_start) {
			ml_append(ml, line, len);
			/* halt_with: start line may itself be the terminator. */
			if (ml->mode == ALLIGATOR_ML_HALT_WITH && is_cond)
				ml_emit(ml, cb, ud);
		} else {
			cb(ud, line, len);
		}
		return;
	}

	switch (ml->mode) {
	case ALLIGATOR_ML_CONTINUE_THROUGH:
		/* Keep lines matching condition; first non-match ends group
		 * (that line may start a new group if it matches start). */
		if (is_cond) {
			ml_append(ml, line, len);
		} else {
			ml_emit(ml, cb, ud);
			if (is_start)
				ml_append(ml, line, len);
			else
				cb(ud, line, len);
		}
		break;

	case ALLIGATOR_ML_CONTINUE_PAST:
		/* Include matching lines plus one additional non-matching line. */
		if (is_cond) {
			ml_append(ml, line, len);
		} else {
			ml_append(ml, line, len);
			ml_emit(ml, cb, ud);
		}
		break;

	case ALLIGATOR_ML_HALT_BEFORE:
		/* Include consecutive lines that do NOT match condition;
		 * a matching condition line starts the next group. */
		if (is_cond) {
			ml_emit(ml, cb, ud);
			ml_append(ml, line, len);
		} else {
			ml_append(ml, line, len);
		}
		break;

	case ALLIGATOR_ML_HALT_WITH:
		/* Include lines up to and including first condition match. */
		ml_append(ml, line, len);
		if (is_cond)
			ml_emit(ml, cb, ud);
		break;
	}
}

void alligator_multiline_flush(alligator_multiline *ml,
			       alligator_ml_cb cb, void *ud)
{
	if (ml)
		ml_emit(ml, cb, ud);
}

/* ---------------- linebuf (physical lines + optional multiline) ---------------- */

void alligator_linebuf_init(alligator_linebuf *lb)
{
	if (!lb)
		return;
	memset(lb, 0, sizeof(*lb));
}

void alligator_linebuf_free(alligator_linebuf *lb)
{
	if (!lb)
		return;
	free(lb->tail);
	alligator_multiline_free(lb->ml);
	memset(lb, 0, sizeof(*lb));
}

int alligator_linebuf_enable_ml(alligator_linebuf *lb,
				const char *start_pattern,
				const char *condition_pattern,
				alligator_ml_mode mode,
				char **err)
{
	if (!lb)
		return -1;
	if (lb->ml) {
		alligator_multiline_free(lb->ml);
		lb->ml = NULL;
	}
	lb->ml = alligator_multiline_new(start_pattern, condition_pattern, mode, err);
	return lb->ml ? 0 : -1;
}

static void linebuf_feed_physical(alligator_linebuf *lb,
				  const char *line, size_t len,
				  alligator_ml_cb cb, void *ud)
{
	if (lb->ml)
		alligator_multiline_feed(lb->ml, line, len, cb, ud);
	else
		cb(ud, line, len);
}

void alligator_linebuf_feed(alligator_linebuf *lb,
			    const char *data, size_t size,
			    alligator_ml_cb cb, void *ud)
{
	if (!lb || !data || !size || !cb)
		return;

	size_t total = lb->tail_len + size;
	char *buf = malloc(total ? total : 1);
	if (!buf)
		return;
	if (lb->tail_len)
		memcpy(buf, lb->tail, lb->tail_len);
	memcpy(buf + lb->tail_len, data, size);

	size_t start = 0;
	for (size_t i = 0; i < total; i++) {
		if (buf[i] != '\n')
			continue;
		size_t line_len = i - start;
		if (line_len && buf[start + line_len - 1] == '\r')
			--line_len;
		linebuf_feed_physical(lb, buf + start, line_len, cb, ud);
		start = i + 1;
	}

	free(lb->tail);
	lb->tail = NULL;
	lb->tail_len = lb->tail_cap = 0;
	if (start < total) {
		size_t rem = total - start;
		lb->tail = malloc(rem + 1);
		if (lb->tail) {
			memcpy(lb->tail, buf + start, rem);
			lb->tail[rem] = '\0';
			lb->tail_len = rem;
			lb->tail_cap = rem + 1;
		}
	}
	free(buf);
}

void alligator_linebuf_flush(alligator_linebuf *lb,
			     alligator_ml_cb cb, void *ud)
{
	if (!lb)
		return;
	if (lb->tail_len) {
		linebuf_feed_physical(lb, lb->tail, lb->tail_len, cb, ud);
		free(lb->tail);
		lb->tail = NULL;
		lb->tail_len = lb->tail_cap = 0;
	}
	if (lb->ml)
		alligator_multiline_flush(lb->ml, cb, ud);
}
