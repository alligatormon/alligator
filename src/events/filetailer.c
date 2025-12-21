#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include "common/file_stat.h"
#include "parsers/multiparser.h"
#include "common/crc32.h"
#include "common/murmurhash.h"
#include "events/context_arg.h"
#include "events/fs_read.h"
#include "main.h"
#include "common/logs.h"
extern aconf* ac;
void filetailer_on_read(uv_fs_t *req);
void file_on_open(uv_fs_t *req);
void blackbox_null(char *metrics, size_t size, context_arg *carg);

typedef struct file_handle {
	uv_fs_t open;
	uv_fs_t read;
	uv_fs_t close;
	char pathname[1024];
	uv_buf_t buffer;
	size_t offset;
	context_arg *carg;
} file_handle;

file_handle *file_handler_struct_init(context_arg *carg, size_t size)
{
	file_handle *fh = calloc(1, sizeof(*fh));

	unsigned int len = size+1;
	char *base = malloc(len);
	fh->buffer = uv_buf_init(base, len);

	fh->carg = carg;
	fh->open.data = fh;
	fh->read.data = fh;
	fh->close.data = fh;

	return fh;
}

void file_handler_struct_free(file_handle *fh)
{
	free(fh->buffer.base);
	free(fh);
}

void filetailer_close(uv_fs_t *req) {
	file_handle *fh = req->data;
	uv_fs_req_cleanup(req);
	context_arg *carg = fh->carg;
	file_handler_struct_free(fh);

	(carg->close_counter)++;
	metric_add_labels4("alligator_open", &carg->open_counter, DATATYPE_UINT, carg, "key", carg->key, "proto", "file", "type", "aggregator", "host", carg->host);
	metric_add_labels4("alligator_close", &carg->close_counter, DATATYPE_UINT, carg, "key", carg->key, "proto", "file", "type", "aggregator", "host", carg->host);
	metric_add_labels4("alligator_read", &carg->read_counter, DATATYPE_UINT, carg, "key", carg->key, "proto", "file", "type", "aggregator", "host", carg->host);
	metric_add_labels4("alligator_read_bytes", &carg->read_bytes_counter, DATATYPE_UINT, carg, "key", carg->key, "proto", "file", "type", "aggregator", "host", carg->host);


	if (carg->period)
		uv_timer_set_repeat(carg->period_timer, carg->period);
}

void file_stat_size_cb(uv_fs_t *req)
{
	context_arg *carg = req->data;

	if (req->result < 0)
	{
		carglog(carg, L_ERROR, "error file_stat_size_cb: %s\n", req->path);
		uv_fs_req_cleanup(req);
		free(req);
		return;
	}

	file_handle *fh = file_handler_struct_init(carg, req->statbuf.st_size);
	strlcpy(fh->pathname, req->path, 1024);

	if (carg->file_stat || carg->checksum || carg->parser_name || carg->calc_lines)
	{
		carglog(carg, L_INFO, "crawl file pathname: %s\n", fh->pathname);

		uint8_t if_selected = 0;
		if (carg->file_stat)
		{
			if_selected = 1;
			uv_fs_t* req_stat = malloc(sizeof(*req_stat));
			req_stat->data = carg;
			uv_fs_stat(carg->loop, req_stat, fh->pathname, file_stat_cb);
		}

		if (carg->checksum || (carg->parser_name && carg->parser_handler != blackbox_null) || carg->calc_lines)
		{
			carglog(carg, L_INFO, "checksum/calc/parser file open: %s in %s\n", carg->checksum, fh->pathname);

			if_selected = 1;
			uv_fs_open(carg->loop, &fh->open, fh->pathname, O_RDONLY, 0, file_on_open);
		}

		if (!if_selected)
			file_handler_struct_free(fh);
	}

	uv_fs_req_cleanup(req);
	free(req);
}

void directory_crawl(void *arg)
{
	context_arg *carg = arg;
	char *path = carg->path;
	uv_fs_t readdir_req;

	uv_fs_opendir(NULL, &readdir_req, path, NULL);
	carglog(carg, L_ERROR, "open dir: %s, status: %p\n", path, readdir_req.ptr);

	if (!readdir_req.ptr)
	{
		carglog(carg, L_ERROR, "open dir: %s, failed, result: %zd\n", path, readdir_req.result);
		return;
	}

	uv_dirent_t dirents[1024];
	uv_dir_t* rdir = readdir_req.ptr;
	rdir->dirents = dirents;
	rdir->nentries = 1024;

	uint64_t acc = 0;

	for(;;)
	{
		int r = uv_fs_readdir(carg->loop, &readdir_req, readdir_req.ptr, NULL);
		if (r <= 0)
		{
			carglog(carg, L_ERROR, "read dir: %s, failed, result: %d\n", path, r);
			break;
		}

		for (int i=0; i<r; i++)
		{
			if (dirents[i].type == UV_DIRENT_FILE && (carg->checksum || carg->parser_name || carg->calc_lines))
			{
				++acc;
				uv_fs_t* req_stat = malloc(sizeof(*req_stat));
				req_stat->data = carg;
				char pathname[1024];
				snprintf(pathname, 1023, "%s%s", carg->path, dirents[i].name);

				carglog(carg, L_INFO, "stat open: %s\n", pathname);

				uv_fs_stat(carg->loop, req_stat, pathname, file_stat_size_cb);
			}
			free((void*)dirents[i].name);
		}
	}

	uv_fs_closedir(NULL, &readdir_req, readdir_req.ptr, NULL);
	++carg->close_counter;
	metric_add_labels("files_in_directory", &acc, DATATYPE_UINT, carg, "path", carg->path);
}

void file_crawl(void *arg)
{
	context_arg *carg = arg;

	uv_fs_t* req_stat = malloc(sizeof(*req_stat));
	req_stat->data = carg;
	uv_fs_stat(carg->loop, req_stat, carg->path, file_stat_size_cb);
}

void filetailer_directory_file_crawl_repeat_period(uv_timer_t *timer);
void filetailer_directory_file_crawl(void *arg)
{
	context_arg *carg = arg;

	if (carg->period && !carg->close_counter) {
		carg->period_timer = alligator_cache_get(ac->uv_cache_timer, sizeof(uv_timer_t));
		carg->period_timer->data = carg;
		uv_timer_init(carg->loop, carg->period_timer);
		uv_timer_start(carg->period_timer, filetailer_directory_file_crawl_repeat_period, carg->period, 0);
	}

	if (carg->is_dir)
		directory_crawl(arg);
	else
		file_crawl(arg);
}

void filetailer_checksum(uv_fs_t *req) {
	file_handle *fh = req->data;
	context_arg *carg = fh->carg;
	uv_fs_req_cleanup(req);

	if (req->result < 0) {
		carglog(carg, L_ERROR, "filetailer_checksum: Read error: %s\n", fh->pathname);
	}
	else if (req->result == 0) {
		carglog(carg, L_ERROR, "filetailer_checksum: No result read: %s\n", fh->pathname);
	}
	else {
		uint64_t str_len = req->result;
		fh->buffer.base[str_len] = 0;
		if (carg->checksum)
		{
			if (!strcmp(carg->checksum, "crc32"))
			{
				uint64_t ret = xcrc32((unsigned char*)fh->buffer.base, str_len, 0);
				metric_add_labels2("file_checksum", &ret, DATATYPE_UINT, carg, "path", fh->pathname, "hash", "crc32");
			}
			else if (!strcmp(carg->checksum, "murmur3"))
			{
				uint64_t ret = murmurhash(fh->buffer.base, str_len, 0);
				metric_add_labels2("file_checksum", &ret, DATATYPE_UINT, carg, "path", fh->pathname, "hash", "murmur3");
			}
		}
		if (carg->calc_lines)
		{
			uint64_t k = 0;
			for (uint64_t i = 0; i < str_len - 1; i++, k++)
			{
				i += strcspn(fh->buffer.base+i, "\n");
			}
			metric_add_labels("file_lines", &k, DATATYPE_UINT, carg, "path", fh->pathname);
		}
	}
	uv_fs_close(carg->loop, &fh->close, fh->open.result, filetailer_close);
}

void file_on_open(uv_fs_t *req)
{
	file_handle *fh = req->data;
	uv_fs_req_cleanup(req);
	context_arg *carg = fh->carg;
	(carg->open_counter)++;
	uint64_t offset = file_stat_get_offset(ac->file_stat, req->path, carg->state);
	if (req->result != -1)
	{
		if (carg->checksum || carg->calc_lines)
		{
			uv_fs_read(carg->loop, &fh->read, req->result, &fh->buffer, 1, 0, filetailer_checksum);
		}
		else if (carg->parser_name)
		{
			carglog(carg, L_INFO, "read from file %s, offset %"u64"\n", req->path, offset);
			fh->offset = offset;
			uv_fs_read(carg->loop, &fh->read, req->result, &fh->buffer, 1, offset, filetailer_on_read);
		}
	}
	else
	{
		carglog(carg, L_ERROR, "Error opening file!\n");
	}
}

void filetailer_on_read(uv_fs_t *req) {
	file_handle *fh = req->data;
	context_arg *carg = fh->carg;
	uv_fs_req_cleanup(req);
	(carg->read_counter)++;

	if (req->result < 0) {
		carglog(carg, L_ERROR, "filetailer_on_read: Read error: %s\n", fh->pathname);
	}
	else if (req->result == 0) {
		carglog(carg, L_ERROR, "filetailer_on_read: No result read: %s\n", fh->pathname);
		file_stat_add_offset(ac->file_stat, fh->pathname, carg, fh->offset);
	}
	else {
		uint64_t str_len = req->result;
		carg->read_bytes_counter += str_len;
		fh->buffer.base[str_len] = 0;

		carglog(carg, L_INFO, "filetailer_on_read: res OK: %s %"u64"\n", fh->pathname, str_len);

		file_stat_add_offset(ac->file_stat, fh->pathname, carg, str_len);
		alligator_multiparser(fh->buffer.base, str_len, carg->parser_handler, NULL, carg);
	}
	uv_fs_close(carg->loop, &fh->close, fh->open.result, filetailer_close);

}

void on_file_change(uv_fs_event_t *handle, const char *filename, int events, int status)
{
	context_arg *carg = handle->data;
	carg->filename = filename;
	carglog(carg, L_INFO, "Change detected in '%s'\n", carg->filename);

	file_handle *fh = file_handler_struct_init(carg, 65535);
	if (carg->is_dir)
		snprintf(fh->pathname, 1023, "%s%s", carg->path, carg->filename);
	else
		snprintf(fh->pathname, 1023, "%s", carg->path);

	if (events == UV_CHANGE)
	{
		uv_fs_open(carg->loop, &fh->open, fh->pathname, O_RDONLY, 0, file_on_open);

		if (carg->file_stat)
		{
			uv_fs_t* req_stat = malloc(sizeof(*req_stat));
			req_stat->data = carg;
			uv_fs_stat(carg->loop, req_stat, fh->pathname, file_stat_cb);
		}
	}
}

char* filetailer_handler(context_arg *carg)
{
	uv_loop_t *loop = carg->loop;

	carg->path = carg->host;

	carg->offset = 0;
	if (carg->path[strlen(carg->path)-1] == '/')
		carg->is_dir = 1;

	carg->fs_handle.data = carg;

	if (carg->key)
		free(carg->key);
	carg->key = strdup(carg->host);

	if ((carg->file_stat || carg->calc_lines || carg->checksum || carg->parser_handler || carg->parser_name) && carg->notify != 2)
	{
		carglog(carg, L_INFO, "create file handler with carg->path %s\n", carg->path);
		alligator_ht_insert(ac->file_aggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));
	}

	if (carg->notify)
	{
		carglog(carg, L_INFO, "run notify %s\n", carg->path);
		uv_fs_event_init(loop, &carg->fs_handle);
		uv_fs_event_start(&carg->fs_handle, on_file_change, carg->path, UV_FS_EVENT_WATCH_ENTRY);
	}

	return "file";
}

void filetailer_handler_del(context_arg *carg)
{
	if (!carg)
		return;

	uv_fs_event_stop(&carg->fs_handle);
	carg_free(carg);
}

void for_filetailer_directory_file_crawl(void *arg)
{
	context_arg *carg = arg;
	if (carg->period && carg->close_counter)
		return;

	filetailer_directory_file_crawl(arg);
}

void filetailer_directory_file_crawl_repeat_period(uv_timer_t *timer)
{
	context_arg *carg = timer->data;
	if (!carg->period)
		return;

	filetailer_directory_file_crawl((void*)carg);
}


void failtailer_crawl(uv_timer_t* handle) {
	(void)handle;
	alligator_ht_foreach(ac->file_aggregator, for_filetailer_directory_file_crawl);
}

void filetailer_crawl_handler()
{
	uv_loop_t *loop = ac->loop;

	uv_timer_init(loop, &ac->filetailer_timer);
	uv_timer_start(&ac->filetailer_timer, failtailer_crawl, ac->aggregator_startup, ac->file_aggregator_repeat);
}

void filetailer_write_state_foreach(void *funcarg, void *arg)
{
	file_stat *fstat = arg;
	string *str = funcarg;

	if (fstat->state != FILESTAT_STATE_SAVE)
		return;

	glog(L_INFO, "save file_stat: %s, %"u64", %"u64"\n", fstat->key, fstat->offset, fstat->modify);

	string_cat(str, "3", 1);
	string_cat(str, "key", 3);
	uint64_t key_len = strlen(fstat->key);
	string_uint(str, key_len);
	string_cat(str, fstat->key, key_len);
	string_cat(str, "\t", 1);
	
	string_cat(str, "6", 1);
	string_cat(str, "offset", 6);
	string_uint(str, fstat->offset);
	string_cat(str, "\t", 1);

	string_cat(str, "6", 1);
	string_cat(str, "modify", 6);
	string_uint(str, fstat->modify);
	string_cat(str, "\n", 1);
}

void filetailer_write_state(alligator_ht *hash)
{
	uint64_t hash_size = alligator_ht_count(hash);
	if (!hash_size)
		return;

	string *str = string_init(1024);
	string_cat(str, "/alligator/file_stat/v1/\n", 25);
	alligator_ht_foreach_arg(hash, filetailer_write_state_foreach, str);
	if (str->l > 25)
	{
		char dirtowrite[255];
		snprintf(dirtowrite, 255, "/var/lib/alligator/file_stat");
		write_to_file(dirtowrite, str->s, str->l, free, str);
	}
	else
		string_free(str);

}

void filestat_restore_v1(char *buf, size_t len)
{
	buf += 25;
	len -= 25;

	uint64_t cur = 0;

	while (cur < len)
	{
		uint64_t n_end = strcspn(buf+cur, "\n") + cur;
		char key[255];
		uint64_t offset = 0;
		*key = 0;
		while (cur < n_end)
		{
			char *tmp = buf;
			uint64_t size = strtoull(buf+cur, &tmp, 10);
			uint64_t t_end = strcspn(buf+cur, "\t") + cur;

			char object[size+1];
			strlcpy(object, tmp, size+1);
			tmp += size;

			uint64_t valsize = strtoull(tmp, &tmp, 10);
			if (*tmp == '\t' || *tmp == '\n')
			{
				glog(L_INFO, "filestat_restore_v1:: %s: %"u64"\n", object, valsize);
				if (!strcmp(object, "offset"))
				{
					offset = valsize;
				}
			}
			else
			{
				char value[valsize+1];
				strlcpy(value, tmp, valsize+1);
				glog(L_INFO, "filestat_restore_v1:: '%s': '%s'\n", object, value);

				if (!strcmp(object, "key"))
				{
					strlcpy(key, value, valsize+1);
				}
			}

			cur = t_end;
			if (buf[t_end] == '\t')
				cur ++;
		}

		if (*key)
			file_stat_add_offset(ac->file_stat, key, NULL, offset);

		cur = n_end+1;
	}
}

void filestat_read_callback(char *buf, size_t len, void *data, char *filename)
{
	if (!strncmp(buf, "/alligator/file_stat/v1/\n", 25))
	{
		filestat_restore_v1(buf, len);
	}
}

void filestat_restore()
{
	char *dirtoread = strdup("/var/lib/alligator/file_stat");
	read_from_file(dirtoread, 0, filestat_read_callback, dirtoread);
}
