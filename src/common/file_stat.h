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

	alligator_ht_node node;
} file_stat;

void file_stat_cb(uv_fs_t *req);
file_stat* file_stat_push(alligator_ht *hash, char *path, context_arg *carg, uv_stat_t *st);
