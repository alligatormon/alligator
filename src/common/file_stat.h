#pragma once
#include <uv.h>
#include "events/context_arg.h"
#include "common/pw.h"
#include "main.h"
#define FILESTAT_STATE_STREAM 0
#define FILESTAT_STATE_BEGIN 1
#define FILESTAT_STATE_SAVE 2
#define FILESTAT_STATE_FORGET 3

typedef struct file_stat {
	char *key;
	uint64_t modify;
	uint64_t offset;
	uint8_t state;

	/* inode of the file the offset refers to; 0 until first open */
	uint64_t dev;
	uint64_t ino;

	/* serialization of parser read chains: only one open->read->close
	 * chain per file may be in flight; concurrent triggers set read_dirty
	 * and the chain is restarted once the current one closes */
	uint8_t read_inflight;
	uint8_t read_dirty;

	alligator_ht_node node;
} file_stat;

void file_stat_cb(uv_fs_t *req);
file_stat* file_stat_push(alligator_ht *hash, char *path, context_arg *carg, uv_stat_t *st);
file_stat* file_stat_get_or_create(alligator_ht *hash, const char *path, uint8_t state);
void file_stat_reset_on_rotation(file_stat *fstat, context_arg *carg, uint64_t filesize);
uint64_t file_stat_get_offset(alligator_ht *hash, const char *path, uint8_t state);
file_stat* file_stat_add_offset(alligator_ht *hash, const char *path, context_arg *carg, uint64_t add_offset);
void file_stat_free(alligator_ht *hash);
