#ifdef __FreeBSD__
#include "main.h"
#include "system/freebsd/sysctl.h"
#include "common/logs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/sysctl.h>

extern aconf *ac;

static int sysctl_read_int64(const char *name, int64_t *out)
{
	size_t sz;

	sz = sizeof(*out);
	if (sysctlbyname(name, out, &sz, NULL, 0) == 0)
		return 1;

	{
		uint64_t uval;
		sz = sizeof(uval);
		if (sysctlbyname(name, &uval, &sz, NULL, 0) == 0) {
			*out = (int64_t)uval;
			return 1;
		}
	}

	{
		int32_t ival;
		sz = sizeof(ival);
		if (sysctlbyname(name, &ival, &sz, NULL, 0) == 0) {
			*out = ival;
			return 1;
		}
	}

	return 0;
}

static void sysctl_get_foreach(void *arg)
{
	sysctl_node *scn = arg;
	int64_t data;

	if (!scn || !scn->name)
		return;

	if (!sysctl_read_int64(scn->name, &data)) {
		if (ac->system_carg && ac->system_carg->log_level > 0)
			carglog(ac->system_carg, L_ERROR, "sysctlbyname failed for %s", scn->name);
		return;
	}

	metric_add_labels("sysctl_data", &data, DATATYPE_INT, ac->system_carg, "name", scn->name);
}

void sysctl_run(alligator_ht *sysctl)
{
	if (!sysctl)
		return;
	alligator_ht_foreach(sysctl, sysctl_get_foreach);
}

static int sysctl_compare(const void *arg, const void *obj)
{
	char *s1 = (char *)arg;
	char *s2 = ((sysctl_node *)obj)->name;
	return strcmp(s1, s2);
}

void sysctl_push(alligator_ht *sysctl, char *sysctl_name)
{
	if (!sysctl || !sysctl_name)
		return;

	uint32_t hash = tommy_strhash_u32(0, sysctl_name);
	sysctl_node *scn = alligator_ht_search(sysctl, sysctl_compare, sysctl_name, hash);
	if (scn)
		return;

	scn = calloc(1, sizeof(*scn));
	if (!scn)
		return;
	scn->name = strdup(sysctl_name);
	if (!scn->name) {
		free(scn);
		return;
	}

	alligator_ht_insert(sysctl, &(scn->node), scn, hash);
}

void sysctl_del(alligator_ht *sysctl, char *sysctl_name)
{
	if (!sysctl || !sysctl_name)
		return;

	uint32_t hash = tommy_strhash_u32(0, sysctl_name);
	sysctl_node *scn = alligator_ht_search(sysctl, sysctl_compare, sysctl_name, hash);
	if (!scn)
		return;

	alligator_ht_remove_existing(sysctl, &(scn->node));
	free(scn->name);
	free(scn);
}

static void sysctl_free_foreach(void *arg)
{
	sysctl_node *scn = arg;
	free(scn->name);
	free(scn);
}

void sysctl_free(alligator_ht *sysctl)
{
	if (!sysctl)
		return;
	alligator_ht_foreach(sysctl, sysctl_free_foreach);
	alligator_ht_done(sysctl);
	free(sysctl);
}
#endif
