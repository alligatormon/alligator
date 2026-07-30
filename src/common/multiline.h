#pragma once
#include <stddef.h>
#include <stdint.h>

/*
 * Vector-compatible multiline log aggregation for alligator.
 *
 * Same model as Vector file/docker sources:
 *   start_pattern      — regex that marks the beginning of a new message
 *   condition_pattern  — regex interpreted according to mode
 *   mode               — continue_through | continue_past | halt_before | halt_with
 *
 * Used by amtail, grok, and vrl so all three share one assembler.
 */

typedef enum {
	ALLIGATOR_ML_CONTINUE_THROUGH = 0,
	ALLIGATOR_ML_CONTINUE_PAST,
	ALLIGATOR_ML_HALT_BEFORE,
	ALLIGATOR_ML_HALT_WITH,
} alligator_ml_mode;

typedef struct alligator_multiline alligator_multiline;

typedef void (*alligator_ml_cb)(void *ud, const char *record, size_t len);

/* Create assembler. start_pattern and condition_pattern are required (Vector).
 * Returns NULL and sets *err (owned) on failure. */
alligator_multiline *alligator_multiline_new(const char *start_pattern,
					     const char *condition_pattern,
					     alligator_ml_mode mode,
					     char **err);
void alligator_multiline_free(alligator_multiline *ml);

/* Feed one physical line (without trailing newline). Emits via cb when a
 * complete logical record is ready. */
void alligator_multiline_feed(alligator_multiline *ml,
			      const char *line, size_t len,
			      alligator_ml_cb cb, void *ud);
void alligator_multiline_flush(alligator_multiline *ml,
			       alligator_ml_cb cb, void *ud);

int alligator_ml_mode_from_str(const char *s, alligator_ml_mode *out);
const char *alligator_ml_mode_to_str(alligator_ml_mode mode);

/*
 * Per-stream helper: incomplete physical-line tail + optional multiline.
 * When ml is NULL, each complete physical line is delivered as a record.
 */
typedef struct alligator_linebuf {
	char *tail;
	size_t tail_len;
	size_t tail_cap;
	alligator_multiline *ml; /* owned or NULL */
} alligator_linebuf;

void alligator_linebuf_init(alligator_linebuf *lb);
void alligator_linebuf_free(alligator_linebuf *lb);

/* Enable Vector multiline on an initialized linebuf. Returns 0 on success. */
int alligator_linebuf_enable_ml(alligator_linebuf *lb,
			       const char *start_pattern,
			       const char *condition_pattern,
			       alligator_ml_mode mode,
			       char **err);

/* Feed a raw byte chunk; invokes cb for each assembled logical record. */
void alligator_linebuf_feed(alligator_linebuf *lb,
			    const char *data, size_t size,
			    alligator_ml_cb cb, void *ud);
void alligator_linebuf_flush(alligator_linebuf *lb,
			     alligator_ml_cb cb, void *ud);
