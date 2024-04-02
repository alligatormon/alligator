#ifdef __linux__
#define _XOPEN_SOURCE 500
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ftw.h>
#include "system/linux/sysctl.h"

void sysctl_copy_name(char *dst, char *src, size_t len)
{
	uint64_t j = 0;
	for (uint64_t i = 10; src[i] && j < 1023; ++i, ++j)
	{
		if (src[i] == '/')
			dst[j] = '.';
		else
			dst[j] = src[i];
	}
	dst[j] = 0;
}

void sysctl_copy_path(char *dst, char *src, size_t len)
{
	strlcpy(dst, "/proc/sys/", len);
	uint64_t i = 10;
	for (uint64_t j = 0; src[j] && i < len; ++i, ++j)
	{
		if (src[j] == '.')
			dst[i] = '/';
		else
			dst[i] = src[j];
	}
	dst[i] = 0;
}

int nftw_info(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf)
{
	if (!S_ISREG(sb->st_mode))
		return 0;

	uint8_t err;
	int64_t data = getkvfile_ext((char*)fpath, &err);
	if (err)
		return 0;

	char new_path[1024];
	sysctl_copy_name(new_path, (char*)fpath, 1024);

	metric_add_labels("sysctl_data", &data, DATATYPE_INT, ac->system_carg, "name", new_path);
	return 0;
}

void sysctl_get_foreach(void *arg)
{
	sysctl_node *scn = arg;
	int flags = 0;
	char full_path[1024];

	sysctl_copy_path(full_path, scn->name, 1024);
	int rc = nftw(full_path, nftw_info, 20, flags);
	if ((rc == -1) && (ac->system_carg->log_level > 0))
	{
		printf("nftw error %s", full_path);
		perror("");
	}

}

void sysctl_run(alligator_ht* sysctl)
{
	alligator_ht_foreach(sysctl, sysctl_get_foreach);
}

int sysctl_compare(const void* arg, const void* obj)
{
        char *s1 = (char*)arg;
        char *s2 = ((sysctl_node*)obj)->name;
        return strcmp(s1, s2);
}

void sysctl_push(alligator_ht *sysctl, char *sysctl_name)
{
	if (!sysctl)
		return;

	uint32_t hash = tommy_strhash_u32(0, sysctl_name);
	sysctl_node *scn = alligator_ht_search(sysctl, sysctl_compare, sysctl_name, hash);
	if (scn)
		return;

	scn = calloc(1, sizeof(*scn));
	scn->name = strdup(sysctl_name);

	alligator_ht_insert(sysctl, &(scn->node), scn, hash);
}

void sysctl_del(alligator_ht* sysctl, char *sysctl_name)
{
	if (!sysctl)
		return;

	uint32_t hash = tommy_strhash_u32(0, sysctl_name);
	sysctl_node *scn = alligator_ht_search(sysctl, sysctl_compare, sysctl_name, hash);
	if (!scn)
		return;

	alligator_ht_remove_existing(sysctl, &(scn->node));

	free(scn->name);
	free(scn);
}

void sysctl_free_foreach(void *arg)
{
	sysctl_node *scn = arg;
	free(scn->name);
	free(scn);
}

void sysctl_free(alligator_ht* sysctl)
{
	alligator_ht_foreach(sysctl, sysctl_free_foreach);
	alligator_ht_done(sysctl);
	free(sysctl);
}
#endif
