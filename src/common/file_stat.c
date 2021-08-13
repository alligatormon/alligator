#include "common/file_stat.h"

void strmode(mode_t mode, char * buf) {
  const char chars[] = "rwxrwxrwx";
  for (size_t i = 0; i < 9; i++) {
    buf[i] = (mode & (1 << (8-i))) ? chars[i] : '-';
  }
  buf[9] = '\0';
}

void file_stat_cb(uv_fs_t *req) {
	if (req->result < 0)
		return;
	if (ac->log_level > 1)
		printf("file_stat_cb run on path: %s\n", req->path);

	context_arg *carg = req->data;
	char *path = (char*)req->path;
	uv_stat_t st = req->statbuf;

	char *type = "";
	if (S_ISDIR(st.st_mode))
		type = "directory";
	else if (S_ISREG(st.st_mode))
		type = "regular";
	else if (S_ISLNK(st.st_mode))
		type = "symlink";
	else if (S_ISCHR(st.st_mode))
		type = "character special file";
	else if (S_ISBLK(st.st_mode))
		type = "block special file";
	else if (S_ISFIFO(st.st_mode))
		type = "fifo";
	else if (S_ISSOCK(st.st_mode))
		type = "socket";

	int64_t atime = st.st_atim.tv_sec;
	int64_t ctime = st.st_ctim.tv_sec;
	int64_t mtime = st.st_mtim.tv_sec;
	int64_t birthtim = st.st_birthtim.tv_sec;
	metric_add_labels("file_stat_size", &st.st_size, DATATYPE_UINT, carg, "path", path);
	metric_add_labels2("file_stat_time", &atime, DATATYPE_INT, carg, "path", path, "type", "atime");
	metric_add_labels2("file_stat_time", &ctime, DATATYPE_INT, carg, "path", path, "type", "ctime");
	metric_add_labels2("file_stat_time", &mtime, DATATYPE_INT, carg, "path", path, "type", "mtime");
	metric_add_labels2("file_stat_time", &birthtim, DATATYPE_INT, carg, "path", path, "type", "birthtime");
	metric_add_labels("file_stat_hardlinks", &st.st_nlink, DATATYPE_INT, carg, "path", path);

	char *user = get_username_by_uid(st.st_uid);
	char *group = get_groupname_by_gid(st.st_gid);

	char modebuf[10];
	char modeint[10];
	strmode(st.st_mode, modebuf);
	snprintf(modeint, 9, "%04o", (unsigned int)st.st_mode);
	
	metric_add_labels6("file_stat_mode", &st.st_mode, DATATYPE_INT, carg, "path", path, "user", user, "group", group, "type", type, "mode", modebuf, "int", modeint);
	free(user);
	free(group);

	file_stat *fstat = file_stat_push(ac->file_stat, path, carg, &st);
	metric_add_labels("file_stat_modify_count", &fstat->modify, DATATYPE_UINT, carg, "path", path);

	fstat->state = carg->state;

	uv_fs_req_cleanup(req);
	free(req);
}

int file_stat_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((file_stat*)obj)->key;
	return strcmp(s1, s2);
}

file_stat* file_stat_push(alligator_ht *hash, char *path, context_arg *carg, uv_stat_t *st)
{
	if (!hash || !path)
		return NULL;

	uint32_t key_hash = tommy_strhash_u32(0, path);
	file_stat *fstat = alligator_ht_search(hash, file_stat_compare, path, key_hash);
	if (!fstat)
	{
		fstat = calloc(1, sizeof(*fstat));
		fstat->key = strdup(path);
		alligator_ht_insert(hash, &(fstat->node), fstat, key_hash);
		carg->files_count++;
		metric_add_labels("file_stat_files_count", &carg->files_count, DATATYPE_UINT, carg, "path", carg->path);
	}

	fstat->modify++;

	return fstat;
}

file_stat* file_stat_add_offset(alligator_ht *hash, char *path, context_arg *carg, uint64_t add_offset)
{
	if (!hash || !path)
		return NULL;

	uint32_t key_hash = tommy_strhash_u32(0, path);
	file_stat *fstat = alligator_ht_search(hash, file_stat_compare, path, key_hash);
	if (!fstat)
	{
		fstat = calloc(1, sizeof(*fstat));
		fstat->key = strdup(path);
		alligator_ht_insert(hash, &(fstat->node), fstat, key_hash);
	}

	if (ac->log_level > 1)
		printf("file_stat_add_offset: set %s offset +%"u64"\n", path, add_offset);
	fstat->modify++;
	fstat->offset += add_offset;

	if (carg)
		fstat->state = carg->state;

	return fstat;
}

uint64_t file_stat_get_offset(alligator_ht *hash, char *path, uint8_t state)
{
	if (!hash || !path)
		return 0;

	uint32_t key_hash = tommy_strhash_u32(0, path);
	file_stat *fstat = alligator_ht_search(hash, file_stat_compare, path, key_hash);
	if (!fstat)
	{
		if (state == FILESTAT_STATE_BEGIN)
		{
			if (ac->log_level > 1)
				printf("file_stat_get_offset: %s BEGIN CASE\n", path);
			return 0;
		}
		else if (state == FILESTAT_STATE_STREAM)
		{
			if (ac->log_level > 1)
				printf("file_stat_get_offset: %s STREAM CASE: %zu\n", path, get_file_size(path));
			return get_file_size(path);
		}
		//fstat = calloc(1, sizeof(*fstat));
		//fstat->key = strdup(path);
		//alligator_ht_insert(hash, &(fstat->node), fstat, key_hash);
	}
	else
	{
		if (ac->log_level > 1)
			printf("file_stat_get_offset: %s SAVE CASE: %"u64"\n", path, fstat->offset);
		return fstat->offset;
	}

	if (ac->log_level > 1)
		printf("file_stat_get_offset: %s UNKNOWN CASE: 0\n", path);
	return 0;
}
