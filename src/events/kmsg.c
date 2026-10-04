#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <uv.h>

#include "main.h"
#include "common/aggregator.h"
#include "common/logs.h"
#include "events/kmsg.h"
#include "events/metrics.h"
#include "parsers/multiparser.h"

extern aconf *ac;

#define KMSG_STATE_MAGIC 0x6b6d7367u
#define KMSG_DRAIN_MAX 64
#define KMSG_REOPEN_MS 1000

typedef struct kmsg_state {
	uint32_t magic;
	int fd;
	int closing;
	char path[1024];
	uv_poll_t *poll;
	uv_timer_t *reopen;
	kmsg_acc acc;
	char readbuf[KMSG_READ_MAX];
	char out[KMSG_ACC_MAX];
	size_t out_len;
} kmsg_state;

const char *kmsg_device_path(const char *host)
{
	if (!host || !host[0])
		return KMSG_DEFAULT_PATH;
	return host;
}

int kmsg_seek_end(int fd)
{
	if (fd < 0)
		return -1;
	/* Offset must be 0: /dev/kmsg rejects any other seek (ESPIPE) and
	 * treats SEEK_END as "past the last record", not a byte length. */
	if (lseek(fd, 0, SEEK_END) == (off_t)-1)
		return -1;
	return 0;
}

int kmsg_io_classify(ssize_t n, int err)
{
	if (n > 0)
		return KMSG_IO_DATA;
	if (n == 0)
		return KMSG_IO_REOPEN;
	if (err == EINTR)
		return KMSG_IO_RETRY;
	if (err == EAGAIN || err == EWOULDBLOCK)
		return KMSG_IO_AGAIN;
	/* Ring overwrote unread records. The next read continues. */
	if (err == EPIPE)
		return KMSG_IO_LOST;
	/* EINVAL means the record did not fit. Re-reading it would spin. */
	return KMSG_IO_REOPEN;
}

int kmsg_acc_append(kmsg_acc *acc, const char *data, size_t n)
{
	if (!acc || (n && !data))
		return -1;
	if (!n)
		return 0;
	if (n > KMSG_ACC_MAX || acc->len + n > KMSG_ACC_MAX)
		return -1;
	memcpy(acc->buf + acc->len, data, n);
	acc->len += n;
	return 0;
}

static int kmsg_hex(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* 1 = line written, 0 = empty message, -1 = no room (out unchanged). */
static int kmsg_write_line(const char *msg, size_t msg_len, char *out, size_t out_cap, size_t *out_len)
{
	size_t o;
	size_t i;
	int any = 0;

	if (!out || !out_len)
		return -1;
	/* Unescape only shrinks (\xHH -> one byte), so msg_len+newline fits or it doesn't. */
	if (*out_len + msg_len + 2 > out_cap)
		return -1;

	o = *out_len;
	for (i = 0; i < msg_len; i++) {
		unsigned char c;

		if (msg[i] == '\\' && i + 3 < msg_len && msg[i + 1] == 'x') {
			int hi = kmsg_hex(msg[i + 2]);
			int lo = kmsg_hex(msg[i + 3]);
			if (hi >= 0 && lo >= 0) {
				c = (unsigned char)((hi << 4) | lo);
				i += 3;
			} else {
				c = (unsigned char)msg[i];
			}
		} else {
			c = (unsigned char)msg[i];
		}
		out[o++] = (char)c;
		any = 1;
	}
	if (!any)
		return 0;
	if (out[o - 1] != '\n')
		out[o++] = '\n';
	*out_len = o;
	if (o < out_cap)
		out[o] = '\0';
	return 1;
}

static int kmsg_find(const char *s, size_t n, char c)
{
	const void *p;

	if (!n)
		return -1;
	p = memchr(s, c, n);
	if (!p)
		return -1;
	return (int)((const char *)p - s);
}

int kmsg_acc_emit(kmsg_acc *acc, char *out, size_t out_cap, size_t *out_len)
{
	size_t off = 0;
	int lines = 0;

	if (!acc || !out || !out_len)
		return 0;
	if (out_cap && *out_len < out_cap)
		out[*out_len] = '\0';

	while (off < acc->len) {
		size_t avail = acc->len - off;
		int semi;
		int nl;
		int wr;
		const char *msg;
		size_t msg_len;

		/* Dictionary lines (leading space) belong to the previous record. */
		if (acc->buf[off] == ' ') {
			nl = kmsg_find(acc->buf + off, avail, '\n');
			if (nl < 0)
				break;
			off += (size_t)nl + 1;
			continue;
		}

		semi = kmsg_find(acc->buf + off, avail, ';');
		if (semi < 0) {
			/* A full read with no header is not a partial record. */
			if (avail > KMSG_READ_MAX)
				off = acc->len;
			break;
		}

		nl = kmsg_find(acc->buf + off, (size_t)semi, '\n');
		if (nl >= 0) {
			/* Junk before the next record. Do not swallow it into the header. */
			off += (size_t)nl + 1;
			continue;
		}

		nl = kmsg_find(acc->buf + off + semi + 1, avail - (size_t)semi - 1, '\n');
		if (nl < 0)
			break;

		msg = acc->buf + off + semi + 1;
		msg_len = (size_t)nl;
		wr = kmsg_write_line(msg, msg_len, out, out_cap, out_len);
		if (wr < 0)
			break;
		if (wr > 0)
			lines++;
		off += (size_t)semi + 1 + msg_len + 1;
	}

	if (off) {
		memmove(acc->buf, acc->buf + off, acc->len - off);
		acc->len -= off;
	}
	return lines;
}

void kmsg_submit_text(context_arg *carg, const char *text, size_t len)
{
	if (!carg || !text || !len)
		return;
	carg->read_counter++;
	carg->read_bytes_counter += len;
	if (carg->parser_handler)
		alligator_multiparser((char *)text, len, carg->parser_handler, NULL, carg);
	else if (carg->log_ch_raw)
		carglog_raw(carg, text, len);
}

static kmsg_state *kmsg_state_of(context_arg *carg)
{
	kmsg_state *st;

	if (!carg || !carg->data)
		return NULL;
	st = carg->data;
	if (st->magic != KMSG_STATE_MAGIC)
		return NULL;
	return st;
}

static void kmsg_flush(context_arg *carg, kmsg_state *st)
{
	int guard = 0;
	int submitted = 0;

	while (guard++ < KMSG_DRAIN_MAX) {
		size_t before = st->out_len;
		int lines = kmsg_acc_emit(&st->acc, st->out, sizeof(st->out), &st->out_len);

		if (lines > 0 && st->acc.len == 0)
			break;
		if (st->out_len > before)
			continue;
		/* No new bytes: either a partial record, or out cannot hold the next line. */
		if (!st->out_len)
			break;
		kmsg_submit_text(carg, st->out, st->out_len);
		st->out_len = 0;
		submitted = 1;
	}
	if (st->out_len) {
		kmsg_submit_text(carg, st->out, st->out_len);
		st->out_len = 0;
		submitted = 1;
	}
	if (submitted && ac)
		aggregator_events_metric_add(carg, carg, NULL, "kmsg", "aggregator", st->path);
}

static void kmsg_start_reopen_timer(kmsg_state *st);

static void kmsg_poll_closed(uv_handle_t *handle)
{
	context_arg *carg = handle->data;
	kmsg_state *st = kmsg_state_of(carg);

	/* Null before free so a late del does not free the handle twice. */
	if (st && st->poll == (uv_poll_t *)handle)
		st->poll = NULL;
	free(handle);
	if (!st)
		return;
	if (st->fd >= 0) {
		close(st->fd);
		st->fd = -1;
	}
	st->acc.len = 0;
	if (!st->closing)
		kmsg_start_reopen_timer(st);
}

static void kmsg_timer_closed(uv_handle_t *handle)
{
	context_arg *carg = handle->data;
	kmsg_state *st = kmsg_state_of(carg);

	if (st && st->reopen == (uv_timer_t *)handle)
		st->reopen = NULL;
	free(handle);
}

static void kmsg_arm_reopen(context_arg *carg, kmsg_state *st)
{
	if (!st || st->closing)
		return;
	if (st->poll && !uv_is_closing((uv_handle_t *)st->poll)) {
		uv_poll_stop(st->poll);
		st->poll->data = carg;
		uv_close((uv_handle_t *)st->poll, kmsg_poll_closed);
		return;
	}
	/* A close already in flight closes the fd. Starting the timer here
	 * would reopen before that callback runs. */
	if (st->poll)
		return;
	if (st->fd >= 0) {
		close(st->fd);
		st->fd = -1;
	}
	kmsg_start_reopen_timer(st);
}

static void kmsg_drain(context_arg *carg, kmsg_state *st)
{
	int rounds = 0;
	int lost = 0;

	while (rounds++ < KMSG_DRAIN_MAX) {
		ssize_t n = read(st->fd, st->readbuf, sizeof(st->readbuf));
		int cls = kmsg_io_classify(n, errno);

		if (cls == KMSG_IO_RETRY)
			continue;
		if (cls == KMSG_IO_AGAIN)
			break;
		if (cls == KMSG_IO_LOST) {
			lost++;
			continue;
		}
		if (cls == KMSG_IO_DATA) {
			if (kmsg_acc_append(&st->acc, st->readbuf, (size_t)n) != 0) {
				kmsg_flush(carg, st);
				if (kmsg_acc_append(&st->acc, st->readbuf, (size_t)n) != 0) {
					st->acc.len = 0;
					(void)kmsg_acc_append(&st->acc, st->readbuf, (size_t)n);
				}
			}
			kmsg_flush(carg, st);
			continue;
		}
		carglog(carg, L_ERROR, "kmsg: read %s: %s\n", st->path, n == 0 ? "eof" : strerror(errno));
		kmsg_flush(carg, st);
		kmsg_arm_reopen(carg, st);
		return;
	}
	kmsg_flush(carg, st);
	if (lost)
		carglog(carg, L_WARN, "kmsg: %s skipped records (EPIPE x%d), continuing\n", st->path, lost);
}

static void kmsg_poll_cb(uv_poll_t *handle, int status, int events)
{
	context_arg *carg = handle->data;
	kmsg_state *st = kmsg_state_of(carg);

	(void)events;
	if (!st || st->closing)
		return;
	if (status < 0) {
		carglog(carg, L_ERROR, "kmsg: poll %s: %s\n", st->path, uv_strerror(status));
		kmsg_arm_reopen(carg, st);
		return;
	}
	kmsg_drain(carg, st);
}

/* 0 = polling, -1 = retry later, -2 = poll close will retry. */
static int kmsg_attach(context_arg *carg, kmsg_state *st)
{
	int fd;
	int err;

	if (st->poll)
		return 0;
	fd = open(st->path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		carglog(carg, L_ERROR, "kmsg: open %s: %s\n", st->path, strerror(errno));
		return -1;
	}
	if (kmsg_seek_end(fd) != 0) {
		carglog(carg, L_ERROR, "kmsg: seek end %s: %s\n", st->path, strerror(errno));
		close(fd);
		return -1;
	}
	st->fd = fd;
	st->acc.len = 0;
	st->poll = calloc(1, sizeof(*st->poll));
	if (!st->poll) {
		close(fd);
		st->fd = -1;
		return -1;
	}
	st->poll->data = carg;
	err = uv_poll_init(carg->loop, st->poll, fd);
	if (err) {
		carglog(carg, L_ERROR, "kmsg: poll init %s: %s\n", st->path, uv_strerror(err));
		free(st->poll);
		st->poll = NULL;
		close(fd);
		st->fd = -1;
		return -1;
	}
	err = uv_poll_start(st->poll, UV_READABLE, kmsg_poll_cb);
	if (err) {
		carglog(carg, L_ERROR, "kmsg: poll start %s: %s\n", st->path, uv_strerror(err));
		uv_close((uv_handle_t *)st->poll, kmsg_poll_closed);
		return -2;
	}
	carg->fd = fd;
	carg->conn_counter++;
	carg->parser_status = 1;
	if (ac)
		aggregator_events_metric_add(carg, carg, NULL, "kmsg", "aggregator", st->path);
	carglog(carg, L_INFO, "kmsg: reading %s from the end (startup does not replay the ring)\n", st->path);
	return 0;
}

static void kmsg_reopen_cb(uv_timer_t *timer)
{
	context_arg *carg = timer->data;
	kmsg_state *st = kmsg_state_of(carg);
	int rc;

	if (!st || st->closing)
		return;
	rc = kmsg_attach(carg, st);
	if (rc == -1)
		kmsg_start_reopen_timer(st);
}

static void kmsg_start_reopen_timer(kmsg_state *st)
{
	if (!st || st->closing || !st->reopen || uv_is_closing((uv_handle_t *)st->reopen))
		return;
	uv_timer_start(st->reopen, kmsg_reopen_cb, KMSG_REOPEN_MS, 0);
}

char *kmsg_handler(context_arg *carg)
{
	kmsg_state *st;
	const char *path;
	int rc;

	if (!carg)
		return NULL;

	st = calloc(1, sizeof(*st));
	if (!st)
		return NULL;
	st->magic = KMSG_STATE_MAGIC;
	st->fd = -1;
	path = kmsg_device_path(carg->host);
	strlcpy(st->path, path, sizeof(st->path));
	carg->data = st;

	if (!carg->loop)
		carg->loop = uv_default_loop();
	if (!carg->loop) {
		free(st);
		carg->data = NULL;
		return NULL;
	}

	st->reopen = calloc(1, sizeof(*st->reopen));
	if (!st->reopen) {
		free(st);
		carg->data = NULL;
		return NULL;
	}
	st->reopen->data = carg;
	uv_timer_init(carg->loop, st->reopen);

	rc = kmsg_attach(carg, st);
	if (rc == -1)
		kmsg_start_reopen_timer(st);
	return "kmsg";
}

void kmsg_handler_del(context_arg *carg)
{
	kmsg_state *st;

	if (!carg)
		return;

	aggregators_ht_unlink(carg);
	st = kmsg_state_of(carg);
	if (!st) {
		carg_free(carg);
		return;
	}

	st->closing = 1;
	if (st->reopen && !uv_is_closing((uv_handle_t *)st->reopen)) {
		uv_timer_stop(st->reopen);
		uv_close((uv_handle_t *)st->reopen, kmsg_timer_closed);
	}
	if (st->poll && !uv_is_closing((uv_handle_t *)st->poll)) {
		uv_poll_stop(st->poll);
		st->poll->data = carg;
		uv_close((uv_handle_t *)st->poll, kmsg_poll_closed);
	} else if (!st->poll && st->fd >= 0) {
		close(st->fd);
		st->fd = -1;
	}

	/* Callbacks null poll/reopen and close the fd. Freeing st before that
	 * leaves libuv writing into a freed context_arg. */
	{
		unsigned i;

		for (i = 0; i < 64 && carg->loop && (st->poll || st->reopen); i++)
			uv_run(carg->loop, UV_RUN_NOWAIT);
	}
	if (st->poll || st->reopen) {
		carglog(carg, L_ERROR, "kmsg: %s close still pending\n", st->path);
		return;
	}
	if (st->fd >= 0)
		close(st->fd);
	free(st);
	carg->data = NULL;
	carg->fd = -1;
	carg_free(carg);
}
