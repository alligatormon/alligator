#pragma once

#include <stddef.h>
#include <sys/types.h>
#include "events/context_arg.h"

/* Kernel printk records are one read() each and at most this long
 * (CONSOLE_EXT_LOG_MAX is 8192; headroom avoids EINVAL retry loops). */
#define KMSG_READ_MAX 65536
#define KMSG_ACC_MAX (KMSG_READ_MAX * 2)
#define KMSG_DEFAULT_PATH "/dev/kmsg"

/* read() classification. EAGAIN stops the drain; EPIPE is a skipped
 * record (keep reading); anything else reopens after a delay. */
enum {
	KMSG_IO_REOPEN = -1,
	KMSG_IO_AGAIN = 0,
	KMSG_IO_DATA = 1,
	KMSG_IO_RETRY = 2,
	KMSG_IO_LOST = 3
};

/* Byte accumulator for records that arrive split across read() calls. */
typedef struct kmsg_acc {
	char buf[KMSG_ACC_MAX];
	size_t len;
} kmsg_acc;

/* Empty host (URL kmsg://) is /dev/kmsg. A non-empty host is the device path
 * (kmsg:///dev/kmsg). */
const char *kmsg_device_path(const char *host);

/* SEEK_END with offset 0. On /dev/kmsg that is "after the last record",
 * so startup does not replay the ring. On a regular file it is EOF. */
int kmsg_seek_end(int fd);

int kmsg_io_classify(ssize_t n, int err);

/* Append raw bytes. Returns 0, or -1 if they do not fit (caller should
 * emit first, then drop a stuck buffer). */
int kmsg_acc_append(kmsg_acc *acc, const char *data, size_t n);

/* Turn complete records into newline-terminated message lines in out.
 * Priority/seq/timestamp and dictionary lines are dropped.
 * Returns the number of lines written. A record that does not fit is
 * left in acc. *out_len is the filled length (not including a trailing NUL,
 * which is written when room remains). */
int kmsg_acc_emit(kmsg_acc *acc, char *out, size_t out_cap, size_t *out_len);

/* Hand decoded text to the aggregate parser (grok, mtail, vrl, …). */
void kmsg_submit_text(context_arg *carg, const char *text, size_t len);

char *kmsg_handler(context_arg *carg);
void kmsg_handler_del(context_arg *carg);
