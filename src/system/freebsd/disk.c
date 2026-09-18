#ifdef __FreeBSD__
#include "main.h"
#include "common/logs.h"
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/disk.h>
#include <libgeom.h>

extern aconf *ac;

static char *geom_config_val(struct gprovider *pp, const char *key)
{
	struct gconfig *gc;

	LIST_FOREACH(gc, &pp->lg_config, lg_config) {
		if (gc->lg_name && !strcmp(gc->lg_name, key))
			return gc->lg_val;
	}
	return NULL;
}

static void emit_disk_ident(struct gprovider *pp)
{
	char *disk = pp->lg_name;
	char *model = geom_config_val(pp, "descr");
	char *serial = geom_config_val(pp, "ident");
	char ident[DISK_IDENT_SIZE];
	char devpath[64];
	uint64_t val = 1;
	uint64_t unalloc = 0;
	int fd;

	if ((!serial || !serial[0])) {
		snprintf(devpath, sizeof(devpath), "/dev/%s", disk);
		fd = open(devpath, O_RDONLY);
		if (fd >= 0) {
			memset(ident, 0, sizeof(ident));
			if (ioctl(fd, DIOCGIDENT, ident) == 0 && ident[0])
				serial = ident;
			close(fd);
		}
	}

	if (model && model[0])
		metric_add_labels2("disk_model", &val, DATATYPE_UINT, ac->system_carg, "model", model, "disk", disk);
	if (serial && serial[0])
		metric_add_labels2("disk_serial", &val, DATATYPE_UINT, ac->system_carg, "serial", serial, "disk", disk);

	if (pp->lg_mediasize > 0)
		metric_add_labels2("disk_size", &pp->lg_mediasize, DATATYPE_UINT, ac->system_carg, "disk", disk, "controller", disk);
	metric_add_labels2("disk_unallocated", &unalloc, DATATYPE_UINT, ac->system_carg, "disk", disk, "controller", disk);
}

void disks_info(void)
{
	struct gmesh mesh;
	struct gclass *cp;
	struct gprovider *pp;
	uint64_t disks_num = 0;

	if (geom_gettree(&mesh) == -1)
		return;

	LIST_FOREACH(cp, &mesh.lg_class, lg_class) {
		if (strcmp(cp->lg_name, "DISK") != 0)
			continue;

		LIST_FOREACH(pp, &cp->lg_prov, lg_prov) {
			if (!pp->lg_name || pp->lg_name[0] == '\0')
				continue;

			emit_disk_ident(pp);
			disks_num++;
		}
	}

	geom_deletetree(&mesh);
	metric_add_auto("disk_num", &disks_num, DATATYPE_UINT, ac->system_carg);
}
#endif
