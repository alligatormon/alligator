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

static void emit_disk_ident(const char *disk, uint64_t size)
{
	char devpath[64];
	struct disk_ident ident;
	uint64_t val = 1;
	uint64_t unalloc = 0;
	int fd;

	snprintf(devpath, sizeof(devpath), "/dev/%s", disk);
	fd = open(devpath, O_RDONLY);
	if (fd >= 0) {
		memset(&ident, 0, sizeof(ident));
		if (ioctl(fd, DIOCGIDENT, &ident) == 0) {
			if (ident.model[0])
				metric_add_labels2("disk_model", &val, DATATYPE_UINT, ac->system_carg, "model", ident.model, "disk", disk);
			if (ident.serial[0])
				metric_add_labels2("disk_serial", &val, DATATYPE_UINT, ac->system_carg, "serial", ident.serial, "disk", disk);
			if (ident.firmware[0])
				metric_add_labels2("disk_firmware", &val, DATATYPE_UINT, ac->system_carg, "firmware", ident.firmware, "disk", disk);
		}
		close(fd);
	}

	if (size > 0)
		metric_add_labels2("disk_size", &size, DATATYPE_UINT, ac->system_carg, "disk", disk, "controller", disk);
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

			emit_disk_ident(pp->lg_name, pp->lg_mediasize);
			disks_num++;
		}
	}

	geom_deletetree(&mesh);
	metric_add_auto("disk_num", &disks_num, DATATYPE_UINT, ac->system_carg);
}
#endif
