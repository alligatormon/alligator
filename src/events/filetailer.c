#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <uv.h>
#include "common/file_stat.h"
#include "parsers/multiparser.h"
#include "common/crc32.h"
#include "common/murmurhash.h"
#include "events/context_arg.h"
#include "events/fs_read.h"
#include "main.h"
#include "common/logs.h"
#include "common/stop.h"
#include "dstructures/ht.h"
extern aconf* ac;
void filetailer_on_read(uv_fs_t *req);
void file_on_open(uv_fs_t *req);
void on_file_change(uv_fs_event_t *handle, const char *filename, int events, int status);
void blackbox_null(char *metrics, size_t size, context_arg *carg);
void filetailer_start_chain(context_arg *carg, const char *pathname);

#define FILETAILER_MAX_READ_SIZE ((size_t)1000000)

typedef struct file_handle {
	uv_fs_t open;
	uv_fs_t read;
	uv_fs_t close;
	char pathname[1024];
	uv_buf_t buffer;
	uint64_t read_offset;
	context_arg *carg;
} file_handle;

file_handle *file_handler_struct_init(context_arg *carg)
{
	file_handle *fh = calloc(1, sizeof(*fh));
	if (!fh)
		return NULL;

	char *base = calloc(1, FILETAILER_MAX_READ_SIZE);
	if (!base) {
		free(fh);
		return NULL;
	}
	fh->buffer = uv_buf_init(base, FILETAILER_MAX_READ_SIZE - 1);

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

/* parser reads track a per-file offset, so only one read chain per file may
 * be in flight at a time: concurrent chains would read the same offset and
 * advance it several times, skipping data and triggering bogus truncation
 * resets. checksum/calc_lines reads are stateless and are not gated. */
static uint8_t filetailer_wants_content_read(context_arg *carg)
{
	if (!carg)
		return 0;
	if (carg->checksum || carg->calc_lines || carg->log_ch_raw)
		return 1;
	if (!carg->parser_handler || carg->parser_handler == blackbox_null)
		return 0;
	return 1;
}

static uint8_t filetailer_is_parser_read(context_arg *carg)
{
	return filetailer_wants_content_read(carg) && !carg->checksum && !carg->calc_lines;
}

/* returns 1 when the chain has to be restarted (new events arrived while it was running) */
static uint8_t filetailer_gate_release(context_arg *carg, const char *pathname, uint8_t allow_restart)
{
	if (!filetailer_is_parser_read(carg))
		return 0;

	file_stat *fstat = file_stat_get_or_create(ac->file_stat, pathname, carg->state);
	if (!fstat)
		return 0;

	fstat->read_inflight = 0;
	if (fstat->read_dirty)
	{
		fstat->read_dirty = 0;
		return allow_restart;
	}

	return 0;
}

static void filetailer_release_open_fd(context_arg *carg, file_handle *fh, int fd)
{
	if (fd != -1)
		close(fd);
	if (carg)
		filetailer_gate_release(carg, fh->pathname, 0);
	file_handler_struct_free(fh);
}

/* Periodic crawl (file_aggregator_repeat) or inotify: read new bytes when present.
 * Offset-tracked reads chain until EOF so one poll tick can batch many lines. */
static void filetailer_schedule_content_read_sz(context_arg *carg, const char *pathname, uint64_t filesize)
{
	file_stat *fst;

	if (!carg || !pathname || !pathname[0])
		return;
	if (!filetailer_wants_content_read(carg))
		return;

	if (filetailer_is_parser_read(carg)) {
		fst = file_stat_get_or_create(ac->file_stat, pathname, carg->state);
		if (fst) {
			if (fst->read_inflight) {
				fst->read_dirty = 1;
				return;
			}
			if (filesize <= fst->offset)
				return;
		}
	}

	filetailer_start_chain(carg, pathname);
}

static void filetailer_schedule_content_read(context_arg *carg, const char *pathname)
{
	filetailer_schedule_content_read_sz(carg, pathname, get_file_size(pathname));
}

static void filetailer_mark_more_data(context_arg *carg, const char *pathname)
{
	if (!filetailer_is_parser_read(carg))
		return;

	file_stat *fstat = file_stat_get_or_create(ac->file_stat, pathname, carg->state);
	if (fstat)
		fstat->read_dirty = 1;
}

static void filetailer_restart_idle_cb(uv_idle_t *handle)
{
	context_arg *carg = handle->data;
	if (!carg || alligator_stop_requested() || carg->lock)
		return;

	carg->filetailer_restart_idle_active = 0;
	uv_idle_stop(handle);

	if (carg->filetailer_restart_path[0])
		filetailer_start_chain(carg, carg->filetailer_restart_path);
}

static void filetailer_schedule_restart(context_arg *carg, const char *pathname)
{
	if (!carg || !pathname || !pathname[0] || alligator_stop_requested() || carg->lock)
		return;

	strlcpy(carg->filetailer_restart_path, pathname, sizeof(carg->filetailer_restart_path));

	if (carg->filetailer_restart_idle_active)
		return;

	if (!carg->filetailer_restart_idle.loop)
		uv_idle_init(carg->loop, &carg->filetailer_restart_idle);

	carg->filetailer_restart_idle.data = carg;
	carg->filetailer_restart_idle_active = 1;
	uv_idle_start(&carg->filetailer_restart_idle, filetailer_restart_idle_cb);
}

static uint64_t filetailer_sync_offset_from_fd(context_arg *carg, const char *pathname, int fd)
{
	file_stat *fst = file_stat_get_or_create(ac->file_stat, pathname, carg->state);
	if (!fst)
		return file_stat_get_offset(ac->file_stat, pathname, carg->state);

	struct stat st;
	if (fstat(fd, &st) != 0)
		return fst->offset;

	uint64_t filesize = (uint64_t)st.st_size;
	uint64_t dev = (uint64_t)st.st_dev;
	uint64_t ino = (uint64_t)st.st_ino;

	if (fst->ino && (fst->dev != dev || fst->ino != ino))
	{
		carglog(carg, L_INFO, "filetailer: inode changed on %s, reset offset (stream=%d, size %"u64")\n",
			pathname, carg->state == FILESTAT_STATE_STREAM, filesize);
		file_stat_reset_on_rotation(fst, carg, filesize);
	}
	else if (fst->offset > filesize)
	{
		carglog(carg, L_INFO, "filetailer: offset %"u64" past end of %s (size %"u64"), clamp\n",
			fst->offset, pathname, filesize);
		file_stat_reset_on_rotation(fst, carg, filesize);
	}

	fst->dev = dev;
	fst->ino = ino;
	return fst->offset;
}

void filetailer_start_chain(context_arg *carg, const char *pathname)
{
	file_stat *fstat = NULL;

	if (!carg || alligator_stop_requested() || carg->lock)
		return;

	if (filetailer_is_parser_read(carg))
	{
		fstat = file_stat_get_or_create(ac->file_stat, pathname, carg->state);
		if (fstat)
		{
			if (fstat->read_inflight)
			{
				fstat->read_dirty = 1;
				return;
			}
			fstat->read_inflight = 1;
			fstat->read_dirty = 0;
		}
	}

	file_handle *fh = file_handler_struct_init(carg);
	if (!fh)
	{
		if (fstat)
			fstat->read_inflight = 0;
		return;
	}

	strlcpy(fh->pathname, pathname, 1024);
	uv_fs_open(carg->loop, &fh->open, fh->pathname, O_RDONLY, 0, file_on_open);
}

void filetailer_close(uv_fs_t *req) {
	file_handle *fh = req->data;
	uv_fs_req_cleanup(req);
	context_arg *carg = fh->carg;

	if (!carg || alligator_stop_requested() || carg->lock) {
		if (carg)
			filetailer_gate_release(carg, fh->pathname, 0);
		file_handler_struct_free(fh);
		return;
	}

	char pathname[1024];
	strlcpy(pathname, fh->pathname, sizeof(pathname));
	file_handler_struct_free(fh);

	(carg->close_counter)++;
	metric_add_labels4("alligator_open_total", &carg->open_counter, DATATYPE_UINT, carg, "key", carg->key, "proto", "file", "type", "aggregator", "host", carg->host);
	metric_add_labels4("alligator_close_total", &carg->close_counter, DATATYPE_UINT, carg, "key", carg->key, "proto", "file", "type", "aggregator", "host", carg->host);
	metric_add_labels4("alligator_read_total", &carg->read_counter, DATATYPE_UINT, carg, "key", carg->key, "proto", "file", "type", "aggregator", "host", carg->host);
	metric_add_labels4("alligator_read_bytes_total", &carg->read_bytes_counter, DATATYPE_UINT, carg, "key", carg->key, "proto", "file", "type", "aggregator", "host", carg->host);


	if (carg->period)
		uv_timer_set_repeat(carg->period_timer, carg->period);

	if (filetailer_gate_release(carg, pathname, 1))
		filetailer_schedule_restart(carg, pathname);
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

	char pathname[1024];
	strlcpy(pathname, req->path, 1024);

	if (req->result >= 0 && (carg->file_stat || filetailer_wants_content_read(carg)))
	{
		uint64_t filesize = (uint64_t)req->statbuf.st_size;

		carglog(carg, L_INFO, "crawl file pathname: %s size %"u64"\n", pathname, filesize);

		if (carg->file_stat)
		{
			uv_fs_t* req_stat = calloc(1, sizeof(*req_stat));
			req_stat->data = carg;
			uv_fs_stat(carg->loop, req_stat, pathname, file_stat_cb);
		}

		filetailer_schedule_content_read_sz(carg, pathname, filesize);
	}

	uv_fs_req_cleanup(req);
	free(req);
}

void directory_crawl(void *arg)
{
	context_arg *carg = arg;
	char *path = carg->path;
	uv_fs_t readdir_req;
	memset(&readdir_req, 0, sizeof(readdir_req));

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
			if (dirents[i].type == UV_DIRENT_FILE && filetailer_wants_content_read(carg))
			{
				++acc;
				uv_fs_t* req_stat = calloc(1, sizeof(*req_stat));
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

	uv_fs_t* req_stat = calloc(1, sizeof(*req_stat));
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
		uv_timer_start(carg->period_timer, filetailer_directory_file_crawl_repeat_period, carg->period, carg->period);
	}

	if (carg->is_dir)
		directory_crawl(arg);
	else
		file_crawl(arg);
}

void filetailer_checksum(uv_fs_t *req) {
	file_handle *fh = req->data;
	context_arg *carg = fh->carg;
	ssize_t nread = req->result;
	uv_fs_req_cleanup(req);

	if (nread < 0) {
		carglog(carg, L_ERROR, "filetailer_checksum: Read error: %s\n", fh->pathname);
	}
	else if (nread == 0) {
		carglog(carg, L_ERROR, "filetailer_checksum: No result read: %s\n", fh->pathname);
	}
	else {
		uint64_t str_len = (uint64_t)nread;
		if (str_len >= fh->buffer.len)
			str_len = fh->buffer.len - 1;
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
	int fd = (int)req->result;
	uv_fs_req_cleanup(req);
	context_arg *carg = fh->carg;

	if (!carg || alligator_stop_requested() || carg->lock) {
		if (fd != -1)
			close(fd);
		if (carg)
			filetailer_gate_release(carg, fh->pathname, 0);
		file_handler_struct_free(fh);
		return;
	}

	(carg->open_counter)++;
	if (fd != -1)
	{
		uint64_t offset;
		if (filetailer_is_parser_read(carg))
			offset = filetailer_sync_offset_from_fd(carg, fh->pathname, fd);
		else
			offset = file_stat_get_offset(ac->file_stat, fh->pathname, carg->state);

		if (carg->checksum || carg->calc_lines)
		{
			uv_fs_read(carg->loop, &fh->read, fd, &fh->buffer, 1, 0, filetailer_checksum);
			return;
		}
		if (filetailer_wants_content_read(carg))
		{
			carglog(carg, L_INFO, "read from file %s, offset %"u64"\n", fh->pathname, offset);
			fh->read_offset = offset;
			uv_fs_read(carg->loop, &fh->read, fd, &fh->buffer, 1, offset, filetailer_on_read);
			return;
		}

		filetailer_release_open_fd(carg, fh, fd);
		return;
	}
	else
	{
		carglog(carg, L_ERROR, "Error opening file: %s\n", fh->pathname);
		filetailer_gate_release(carg, fh->pathname, 0);
		file_handler_struct_free(fh);
	}
}

void filetailer_on_read(uv_fs_t *req) {
	file_handle *fh = req->data;
	context_arg *carg = fh->carg;
	ssize_t nread = req->result;
	uv_fs_req_cleanup(req);

	if (!carg || alligator_stop_requested() || carg->lock) {
		if (fh->open.result != -1)
			close((int)fh->open.result);
		if (carg)
			filetailer_gate_release(carg, fh->pathname, 0);
		file_handler_struct_free(fh);
		return;
	}

	(carg->read_counter)++;

	if (nread < 0) {
		carglog(carg, L_ERROR, "filetailer_on_read: Read error: %s\n", fh->pathname);
	}
	else if (nread == 0) {
		uint64_t filesize = get_file_size(fh->pathname);
		file_stat *fst = file_stat_get_or_create(ac->file_stat, fh->pathname, carg->state);
		if (fst && fh->read_offset > filesize) {
			carglog(carg, L_INFO, "filetailer_on_read: file shrunk %s, offset %"u64" > size %"u64", clamp (stream=%d)\n",
					fh->pathname, fh->read_offset, filesize, carg->state == FILESTAT_STATE_STREAM);
			file_stat_reset_on_rotation(fst, carg, filesize);
			fh->read_offset = fst->offset;
		} else {
			carglog(carg, L_INFO, "filetailer_on_read: EOF reached: %s, offset %"u64", size %"u64"\n",
					fh->pathname, fh->read_offset, filesize);
			if (fst && fh->read_offset < filesize)
				filetailer_mark_more_data(carg, fh->pathname);
		}
	}
	else {
		size_t buf_cap = fh->buffer.len;
		if (fh->buffer.base && buf_cap > 0 && nread > 0) {
			size_t str_len = (size_t)nread;
			if (str_len >= buf_cap)
				str_len = buf_cap - 1;
			carg->read_bytes_counter += str_len;
			fh->buffer.base[str_len] = '\0';

			carglog(carg, L_INFO, "filetailer_on_read: res OK: %s %"u64"\n", fh->pathname, (uint64_t)str_len);

			file_stat_add_offset(ac->file_stat, fh->pathname, carg, str_len);
			alligator_multiparser(fh->buffer.base, str_len, carg->parser_handler, NULL, carg);

			uint64_t new_offset = fh->read_offset + str_len;
			uint64_t filesize = get_file_size(fh->pathname);
			if (new_offset < filesize)
				filetailer_mark_more_data(carg, fh->pathname);
		}
	}
	uv_fs_close(carg->loop, &fh->close, fh->open.result, filetailer_close);

}

static void filetailer_fs_rearm_close_cb(uv_handle_t *handle)
{
	context_arg *carg;

	if (!handle)
		return;

	carg = handle->data;
	if (!carg || alligator_stop_requested() || carg->lock)
		return;

	uv_fs_event_init(carg->loop, (uv_fs_event_t *)handle);
	handle->data = carg;
	carg->fs_handle.data = carg;
	uv_fs_event_start((uv_fs_event_t *)handle, on_file_change, carg->path, UV_FS_EVENT_WATCH_ENTRY);
}

void on_file_change(uv_fs_event_t *handle, const char *filename, int events, int status)
{
	context_arg *carg = handle->data;
	carg->filename = filename;
	carglog(carg, L_INFO, "Change detected in '%s': %d\n", carg->filename, events);

	char pathname[1024];
	if (carg->is_dir)
		snprintf(pathname, 1023, "%s%s", carg->path, carg->filename);
	else
		snprintf(pathname, 1023, "%s", carg->path);

	if (events & UV_RENAME) {
		struct stat st;
		file_stat *fst = file_stat_get_or_create(ac->file_stat, pathname, carg->state);
		if (fst) {
			if (stat(pathname, &st) == 0) {
				carglog(carg, L_INFO, "filetailer: rename on %s, reset offset (size %zu)\n",
					pathname, (size_t)st.st_size);
				file_stat_reset_on_rotation(fst, carg, (uint64_t)st.st_size);
				fst->dev = (uint64_t)st.st_dev;
				fst->ino = (uint64_t)st.st_ino;
			} else {
				file_stat_reset_on_rotation(fst, carg, 0);
			}
		}

		if (!carg->is_dir) {
			carglog(carg, L_INFO, "filetailer: re-arm watcher on rename for %s\n", carg->path);
			uv_fs_event_stop(&carg->fs_handle);
			if (!uv_is_closing((uv_handle_t *)&carg->fs_handle))
				uv_close((uv_handle_t *)&carg->fs_handle, filetailer_fs_rearm_close_cb);
		}
	}

	if (events & UV_CHANGE)
		filetailer_schedule_content_read(carg, pathname);

	if ((events & UV_CHANGE) && carg->file_stat)
	{
		uv_fs_t* req_stat = calloc(1, sizeof(*req_stat));
		req_stat->data = carg;
		uv_fs_stat(carg->loop, req_stat, pathname, file_stat_cb);
	}
}

char* filetailer_handler(context_arg *carg)
{
	uv_loop_t *loop = carg->loop;

	carg->path = carg->host;

	if (carg->path[strlen(carg->path)-1] == '/')
		carg->is_dir = 1;

	carg->fs_handle.data = carg;

	if (carg->key)
		free(carg->key);
	carg->key = strdup(carg->host);

	if ((carg->file_stat || carg->calc_lines || carg->checksum || carg->parser_handler ||
	     carg->parser_name || carg->log_ch_raw) && carg->notify != 2)
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

	if (carg->state == FILESTAT_STATE_STREAM) {
		file_stat_get_offset(ac->file_stat, carg->path, carg->state);
	}

	return "file";
}

static void filetailer_drain_loop(uv_loop_t *loop, context_arg *carg)
{
	unsigned i;

	if (!loop || !carg)
		return;

	for (i = 0; i < 64; ++i) {
		int pending = 0;

		if (carg->filetailer_restart_idle.loop)
			pending = 1;
		if (carg->notify && carg->fs_handle.loop)
			pending = 1;
		if (!pending)
			break;
		uv_run(loop, UV_RUN_NOWAIT);
	}
}

void filetailer_shutdown(void)
{
	uv_loop_t *loop;
	unsigned i;

	if (!ac)
		return;

	loop = ac->loop;
	if (!loop)
		return;

	if (ac->filetailer_timer.loop && !uv_is_closing((uv_handle_t *)&ac->filetailer_timer)) {
		uv_timer_stop(&ac->filetailer_timer);
		uv_close((uv_handle_t *)&ac->filetailer_timer, NULL);
		for (i = 0; i < 64; ++i)
			uv_run(loop, UV_RUN_NOWAIT);
	}
}

static void filetailer_close_uv_handles(context_arg *carg)
{
	uv_loop_t *loop;

	if (!carg)
		return;

	loop = carg->loop;
	carg_uv_detach_timers(carg);

	if (carg->filetailer_restart_idle.loop &&
	    !uv_is_closing((uv_handle_t *)&carg->filetailer_restart_idle)) {
		uv_idle_stop(&carg->filetailer_restart_idle);
		uv_close((uv_handle_t *)&carg->filetailer_restart_idle, NULL);
		carg->filetailer_restart_idle_active = 0;
	}

	if (carg->notify &&
	    carg->fs_handle.loop &&
	    !uv_is_closing((uv_handle_t *)&carg->fs_handle)) {
		uv_fs_event_stop(&carg->fs_handle);
		uv_close((uv_handle_t *)&carg->fs_handle, NULL);
	}

	filetailer_drain_loop(loop, carg);
}

void filetailer_handler_del(context_arg *carg)
{
	if (!carg)
		return;

	carg->lock = 1;

	if (ac && ac->file_aggregator)
		alligator_ht_remove_existing(ac->file_aggregator, &(carg->node));

	filetailer_close_uv_handles(carg);
	carg_free(carg);
}

void for_filetailer_directory_file_crawl(void *arg)
{
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
	const size_t FILETAILER_RESTORE_FIELD_MAX = 1024;
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

			char object[FILETAILER_RESTORE_FIELD_MAX];
			size_t object_copy = size < (sizeof(object) - 1) ? size : (sizeof(object) - 1);
			strlcpy(object, tmp, object_copy + 1);
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
				char value[FILETAILER_RESTORE_FIELD_MAX];
				size_t value_copy = valsize < (sizeof(value) - 1) ? valsize : (sizeof(value) - 1);
				strlcpy(value, tmp, value_copy + 1);
				glog(L_INFO, "filestat_restore_v1:: '%s': '%s'\n", object, value);

				if (!strcmp(object, "key"))
				{
					strlcpy(key, value, sizeof(key));
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
