#include <stdio.h>
#include <string.h>
#include "main.h"
#include "common/deb.h"
#include "common/logs.h"
#include "common/selector.h"
#include "events/fs_read.h"
#include "metric/metric_types.h"
#include "metric/namespace.h"
extern aconf *ac;

void packages_register_metric_families(context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "package_installed", METRIC_TYPE_GAUGE,
		"Unix timestamp when the package was installed, labeled by name, version, release and arch.");
	namespace_metric_family_set(NULL, carg, "package_total", METRIC_TYPE_GAUGE,
		"Total number of installed packages seen during the last scrape.");
}

// Returns the value of the `name` field inside the [stanza, end) paragraph.
// Fields are matched at the beginning of a line only, so neither Config-Version:
// nor the text of Description: continuation lines can be picked up by mistake.
static char *deb_field(char *stanza, char *end, const char *name, size_t namelen, size_t *vlen)
{
	char *p = stanza;

	while (p < end)
	{
		char *eol = memchr(p, '\n', (size_t)(end - p));
		if (!eol)
			eol = end;

		if (((size_t)(eol - p) > namelen) && !strncmp(p, name, namelen) && (p[namelen] == ':'))
		{
			char *value = p + namelen + 1;
			while ((value < eol) && ((*value == ' ') || (*value == '\t')))
				++value;

			*vlen = (size_t)(eol - value);
			return value;
		}

		p = eol + 1;
	}

	*vlen = 0;
	return NULL;
}

// The Status field is "<want> <flag> <status>". Only install/hold + installed
// means the package is present: config-files, not-installed, half-installed
// and unpacked stanzas are still kept in /var/lib/dpkg/status.
static int8_t deb_is_installed(const char *value, size_t vlen)
{
	const char *last = value + vlen;
	const char *state = last;

	while ((state > value) && (state[-1] != ' '))
		--state;

	if (((size_t)(last - state) != 9) || strncmp(state, "installed", 9))
		return 0;

	if (!strncmp(value, "install ", 8) || !strncmp(value, "hold ", 5))
		return 1;

	return 0;
}

static void deb_copy(char *dst, size_t dstsize, const char *src, size_t srclen)
{
	size_t copy = srclen < (dstsize - 1) ? srclen : (dstsize - 1);

	if (copy)
		memcpy(dst, src, copy);

	dst[copy] = 0;
}

void dpkg_list(char *str, size_t len)
{
	packages_register_metric_families(ac->system_carg);

	char pkgname[256];
	char versionname[256];
	char releasename[256];
	char archname[64];

	uint64_t pkgs = 0;
	char *p = str;
	char *end = str + len;

	while (p < end)
	{
		char *line = p;
		char *stanza_end = end;

		while (line < end)
		{
			char *eol = memchr(line, '\n', (size_t)(end - line));
			if (!eol)
				break;

			if (eol == line)
			{
				stanza_end = line;
				break;
			}

			line = eol + 1;
		}

		size_t namelen = 0;
		size_t verlen = 0;
		size_t statuslen = 0;
		size_t archlen = 0;
		char *name = deb_field(p, stanza_end, "Package", 7, &namelen);
		char *status = deb_field(p, stanza_end, "Status", 6, &statuslen);
		char *version = deb_field(p, stanza_end, "Version", 7, &verlen);
		char *arch = deb_field(p, stanza_end, "Architecture", 12, &archlen);

		p = (stanza_end < end) ? (stanza_end + 1) : end;

		if (!name || !namelen || !version || !verlen)
			continue;

		if (!status || !deb_is_installed(status, statuslen))
			continue;

		deb_copy(pkgname, sizeof(pkgname), name, namelen);
		deb_copy(archname, sizeof(archname), arch ? arch : "", archlen);

		// epoch is not a part of the upstream version: 2:8.2.1-1ubuntu1
		char *epoch = memchr(version, ':', verlen);
		if (epoch)
		{
			verlen -= (size_t)(epoch + 1 - version);
			version = epoch + 1;
		}

		// debian revision follows the last hyphen, not the first one
		char *revision = NULL;
		for (size_t i = 0; i < verlen; i++)
			if (version[i] == '-')
				revision = version + i;

		if (revision)
		{
			deb_copy(versionname, sizeof(versionname), version, (size_t)(revision - version));
			deb_copy(releasename, sizeof(releasename), revision + 1, verlen - (size_t)(revision + 1 - version));
		}
		else
		{
			deb_copy(versionname, sizeof(versionname), version, verlen);
			*releasename = 0;
		}

		++pkgs;

		if (!match_mapper(ac->packages_match, pkgname, strlen(pkgname), pkgname))
			continue;

		glog(L_TRACE, "package: %s, version: %s, release: %s, arch: %s\n", pkgname, versionname, releasename, archname);

		// dpkg does not store the installation time
		int64_t datetime = 1;
		metric_add_labels4("package_installed", &datetime, DATATYPE_INT, ac->system_carg, "name", pkgname, "release", releasename, "version", versionname, "arch", archname);
	}

	metric_add_auto("package_total", &pkgs, DATATYPE_UINT, ac->system_carg);
}

void dpkg_callback(char *buf, size_t len, void *data, char *filename)
{
	if (buf && len > 1)
		dpkg_list(buf, len);
}

void dpkg_stat_cb(uv_fs_t* req)
{
	ssize_t result = req->result;
	char *path = req->data;

	uv_fs_req_cleanup(req);
	free(req);

	if (result < 0)
		return;

	read_whole_file(strdup(path), dpkg_callback, NULL);
}

void dpkg_crawl(char *path)
{
	uv_fs_t *req = calloc(1, sizeof(uv_fs_t));
	req->data = path;
	uv_fs_stat(uv_default_loop(), req, path, dpkg_stat_cb);
}
