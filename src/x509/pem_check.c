#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "common/rtime.h"
#include "x509/type.h"
#include "common/lcrypto.h"
#include "scheduler/type.h"
#include "modules/modules.h"
#include "events/fs_read.h"
#include "scheduler/type.h"
#include "common/logs.h"
#include "main.h"

// TODO: function read_all_file from selector.c more flexiblw
char *read_file(char *name)
{
	FILE *fd = fopen(name, "r");
	if (!fd)
		return 0;

	char *pem_cert = malloc(2049);
	size_t rc = fread(pem_cert, 1, 2048, fd);
	fclose(fd);
	if (!rc)
	{
		free(pem_cert);
		return NULL;
	}
	pem_cert[rc] = 0;

	return pem_cert;
}

void fs_cert_check(x509_fs_t *tls_fs, char *fname)
{
	void *func = libcrypto_pem_check_cert;
	if (tls_fs->type == X509_TYPE_PFX)
		func = libcrypto_p12_check_cert;

	int found = 0;
	for (uint64_t i = 0; i < tls_fs->match->l; ++i) {
		if (strstr(fname, tls_fs->match->str[i]->s)) {
			read_from_file(fname, 0, func, &tls_fs->fctx);
			found = 1;
			break;
		}
	}
	if (!found)
		free(fname);
}

//int min(int a, int b) { return (a < b)? a : b;  }
int tls_fs_dir_read(x509_fs_t *tls_fs, char *path)
{
	uv_fs_t readdir_req;

	uv_fs_opendir(NULL, &readdir_req, path, NULL);
	glog(L_DEBUG, "tls open dir: %s\n", path);

	if (!readdir_req.ptr)
		return 0;

	uv_dirent_t dirents[1024];
	uv_dir_t* rdir = readdir_req.ptr;
	rdir->dirents = dirents;
	rdir->nentries = 1024;//min(1024,chunklen);

	int acc = 0;

	char fullname[1024];
	size_t path_len = strnlen(path, sizeof(fullname) - 1);
	strlcpy(fullname, path, path_len + 1);
	if (path_len + 1 >= sizeof(fullname))
		return 0;

	char * filebase = fullname + path_len + 1;
	*(filebase - 1) = '/';

	for(;;)
	{
		int r = uv_fs_readdir(uv_default_loop(), &readdir_req, readdir_req.ptr, NULL);
		if (r <= 0)
			break;

		for (int i=0; i<r; i++)
		{
			if (dirents[i].type == UV_DIRENT_DIR)
			{
				size_t remain = sizeof(fullname) - (size_t)(filebase - fullname);
				if (remain > 1) {
					strlcpy(filebase, dirents[i].name, remain);
					acc += tls_fs_dir_read(tls_fs, fullname);
				}
			}
			else if (dirents[i].type == UV_DIRENT_FILE || dirents[i].type == UV_DIRENT_LINK)
			{
				//printf("\tpath '%s', dirents[i].name '%s'\n", path, dirents[i].name);
				acc += 1;
				char *filename = malloc(1024);
				snprintf(filename, 1023, "%s/%s", path, dirents[i].name);
				fs_cert_check(tls_fs, filename);
			}
			free((void*)dirents[i].name);
		}
	}

	uv_fs_closedir(NULL, &readdir_req, readdir_req.ptr, NULL);
	return acc;
}

void tls_fs_recurse(void *arg)
{
	if (!arg)
		return;

	x509_fs_t *tls_fs = arg;

	if (tls_fs->crawl_busy)
		return;
	tls_fs->crawl_busy = 1;
	tls_fs_dir_read(tls_fs, tls_fs->path);
	tls_fs->crawl_busy = 0;
}

void for_tls_fs_recurse(void *arg)
{
	x509_fs_t *tls_fs = arg;
	if (tls_fs->period)
		return;

	tls_fs_recurse(arg);
}

void for_tls_fs_recurse_repeat_period(uv_timer_t *timer)
{
	x509_fs_t *tls_fs = timer->data;
	if (!tls_fs->period)
		return;

	uv_timer_set_repeat(tls_fs->period_timer, tls_fs->period);

	tls_fs_recurse((void*)tls_fs);
}

static void tls_fs_crawl(uv_timer_t* handle) {
	(void)handle;
	alligator_ht_foreach(ac->fs_x509, for_tls_fs_recurse);
}

void tls_fs_handler()
{
	uv_loop_t *loop = ac->loop;

	uv_timer_init(loop, &ac->tls_fs_timer);
	uv_timer_start(&ac->tls_fs_timer, tls_fs_crawl, ac->tls_fs_startup, ac->tls_fs_repeat);
}

