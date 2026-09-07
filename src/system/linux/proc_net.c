#ifdef __linux__

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <inttypes.h>
#include "main.h"
#include "common/logs.h"
#include "common/selector.h"
#include "system/linux/proc_net.h"

extern aconf *ac;

void get_softnet_stats(void)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/net/softnet_stat", ac->system_procfs);
	carglog(ac->system_carg, L_TRACE, "system scrape metrics: network: softnet '%s'\n", path);

	FILE *fd = fopen(path, "r");
	if (!fd)
		return;

	char line[512];
	uint64_t cpu = 0;
	while (fgets(line, sizeof(line), fd)) {
		char cpu_label[16];
		uint64_t processed = 0, dropped = 0, squeezed = 0;
		int n = sscanf(line, "%" SCNx64 " %" SCNx64 " %" SCNx64, &processed, &dropped, &squeezed);
		if (n < 1)
			continue;

		snprintf(cpu_label, sizeof(cpu_label), "%" PRIu64, cpu);
		metric_add_labels("softnet_processed_total", &processed, DATATYPE_UINT,
			ac->system_carg, "cpu", cpu_label);
		if (n >= 2)
			metric_add_labels("softnet_dropped_total", &dropped, DATATYPE_UINT,
				ac->system_carg, "cpu", cpu_label);
		if (n >= 3)
			metric_add_labels("softnet_times_squeezed_total", &squeezed, DATATYPE_UINT,
				ac->system_carg, "cpu", cpu_label);
		++cpu;
	}
	fclose(fd);
}

static void parse_sockstat_file(const char *path)
{
	carglog(ac->system_carg, L_TRACE, "system scrape metrics: network: sockstat '%s'\n", path);

	FILE *fd = fopen(path, "r");
	if (!fd)
		return;

	char line[512];
	while (fgets(line, sizeof(line), fd)) {
		char *cur = line;
		cur += strspn(cur, " \t");
		if (!*cur || *cur == '\n')
			continue;

		char proto[32];
		size_t proto_len = strcspn(cur, ": \t");
		if (!proto_len || proto_len >= sizeof(proto))
			continue;
		strlcpy(proto, cur, proto_len + 1);
		cur += proto_len;
		cur += strspn(cur, ": \t");

		if (!strcmp(proto, "sockets")) {
			while (*cur) {
				char key[32];
				uint64_t val = 0;
				if (sscanf(cur, "%31s %" SCNu64, key, &val) != 2)
					break;
				if (!strcmp(key, "used")) {
					metric_add_auto("sockstat_sockets_used", &val, DATATYPE_UINT, ac->system_carg);
					break;
				}
				cur += strcspn(cur, " \t");
				cur += strspn(cur, " \t");
			}
			continue;
		}

		while (*cur) {
			char stat[32];
			uint64_t val = 0;
			if (sscanf(cur, "%31s %" SCNu64, stat, &val) != 2)
				break;
			metric_add_labels2("sockstat_stat_total", &val, DATATYPE_UINT,
				ac->system_carg, "protocol", proto, "stat", stat);
			cur += strcspn(cur, " \t");
			cur += strspn(cur, " \t");
		}
	}
	fclose(fd);
}

void get_sockstat_stats(void)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/net/sockstat", ac->system_procfs);
	parse_sockstat_file(path);
	snprintf(path, sizeof(path), "%s/net/sockstat6", ac->system_procfs);
	parse_sockstat_file(path);
}

void get_bonding_stats(void)
{
	char masters_path[512];
	snprintf(masters_path, sizeof(masters_path), "%s/class/net/bonding_masters", ac->system_sysfs);
	FILE *fd = fopen(masters_path, "r");
	if (!fd)
		return;

	char line[512];
	if (!fgets(line, sizeof(line), fd)) {
		fclose(fd);
		return;
	}
	fclose(fd);

	char *master = line;
	while (*master) {
		master += strspn(master, " \t\n");
		if (!*master)
			break;
		char *end = master + strcspn(master, " \t\n");
		if (*end)
			*end++ = '\0';

		char slaves_path[768];
		snprintf(slaves_path, sizeof(slaves_path), "%s/class/net/%s/bonding/slaves", ac->system_sysfs, master);
		FILE *sfd = fopen(slaves_path, "r");
		if (!sfd) {
			master = end;
			continue;
		}

		uint64_t total = 0, active = 0;
		char sline[512];
		if (fgets(sline, sizeof(sline), sfd)) {
			char *slave = sline;
			while (*slave) {
				slave += strspn(slave, " \t\n");
				if (!*slave)
					break;
				char *send = slave + strcspn(slave, " \t\n");
				if (*send)
					*send++ = '\0';
				++total;

				char status_path[1024];
				snprintf(status_path, sizeof(status_path),
					"%s/class/net/%s/lower_%s/bonding_slave/mii_status", ac->system_sysfs, master, slave);
				char status[16] = "";
				getkvfile_str(status_path, status, sizeof(status));
				if (!status[0]) {
					snprintf(status_path, sizeof(status_path),
						"%s/class/net/%s/slave_%s/bonding_slave/mii_status", ac->system_sysfs, master, slave);
					getkvfile_str(status_path, status, sizeof(status));
				}
				if (!strcmp(status, "up"))
					++active;

				slave = send;
			}
		}
		fclose(sfd);

		metric_add_labels2("bonding_slaves", &total, DATATYPE_UINT,
			ac->system_carg, "master", master, "type", "total");
		metric_add_labels2("bonding_slaves", &active, DATATYPE_UINT,
			ac->system_carg, "master", master, "type", "active");

		char bond_dir[768];
		char val[64];
		snprintf(bond_dir, sizeof(bond_dir), "%s/class/net/%s/bonding", ac->system_sysfs, master);
		struct {
			char *file;
			char *stat;
		} lacp[] = {
			{ "ad_num_ports", "ad_num_ports" },
			{ "ad_actor_key", "ad_actor_key" },
			{ "ad_partner_key", "ad_partner_key" },
			{ "ad_aggregator", "ad_aggregator" },
		};
		for (size_t i = 0; i < sizeof(lacp) / sizeof(lacp[0]); ++i) {
			char fpath[900];
			snprintf(fpath, sizeof(fpath), "%s/%s", bond_dir, lacp[i].file);
			int64_t n = getkvfile(fpath);
			if (n >= 0)
				metric_add_labels2("bonding_lacp", &n, DATATYPE_INT, ac->system_carg,
					"master", master, "stat", lacp[i].stat);
		}
		{
			char fpath[900];
			snprintf(fpath, sizeof(fpath), "%s/mode", bond_dir);
			if (getkvfile_str(fpath, val, sizeof(val)) > 0) {
				/* mode is often "4" or "802.3ad 4" — export numeric prefix */
				int64_t mode = atoll(val);
				metric_add_labels2("bonding_lacp", &mode, DATATYPE_INT, ac->system_carg,
					"master", master, "stat", "mode");
			}
		}

		master = end;
	}
}

void get_arp_stats(void)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/net/arp", ac->system_procfs);
	FILE *fd = fopen(path, "r");
	if (!fd)
		return;

	char line[512];
	if (!fgets(line, sizeof(line), fd)) {
		fclose(fd);
		return;
	}

	char devices[64][32];
	uint64_t counts[64];
	size_t ndev = 0;

	while (fgets(line, sizeof(line), fd)) {
		char ip[64], hwtype[32], flags[32], hwaddr[32], mask[32], device[32];
		if (sscanf(line, "%63s %31s %31s %31s %31s %31s",
			ip, hwtype, flags, hwaddr, mask, device) != 6)
			continue;

		size_t i;
		for (i = 0; i < ndev; ++i) {
			if (!strcmp(devices[i], device))
				break;
		}
		if (i == ndev && ndev < 64) {
			strlcpy(devices[ndev], device, sizeof(devices[ndev]));
			counts[ndev] = 0;
			++ndev;
		}
		if (i < ndev)
			++counts[i];
	}
	fclose(fd);

	for (size_t i = 0; i < ndev; ++i)
		metric_add_labels("arp_entries", &counts[i], DATATYPE_UINT,
			ac->system_carg, "device", devices[i]);
}

void get_ipvs_stats(void)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/net/ip_vs_stats", ac->system_procfs);
	FILE *fd = fopen(path, "r");
	if (!fd)
		return;

	char header[512] = "";
	char values[512] = "";
	if (!fgets(header, sizeof(header), fd)) {
		fclose(fd);
		return;
	}

	char *colon = strchr(header, ':');
	if (colon) {
		while (fgets(values, sizeof(values), fd)) {
			char key[64];
			uint64_t val = 0;
			char *c = strchr(values, ':');
			if (!c)
				continue;
			size_t klen = c - values;
			if (!klen || klen >= sizeof(key))
				continue;
			strlcpy(key, values, klen + 1);
			val = strtoull(c + 1, NULL, 10);
			for (size_t i = 0; key[i]; ++i)
				key[i] = (char)tolower((unsigned char)key[i]);
			metric_add_labels("ipvs_stat_total", &val, DATATYPE_UINT,
				ac->system_carg, "stat", key);
		}
		fclose(fd);
		return;
	}

	if (!fgets(values, sizeof(values), fd)) {
		fclose(fd);
		return;
	}
	fclose(fd);

	char *keys[32];
	char *vals[32];
	size_t n = 0;
	char *kcur = header;
	char *vcur = values;
	while (n < 32) {
		kcur += strspn(kcur, " \t\n");
		vcur += strspn(vcur, " \t\n");
		if (!*kcur || !*vcur)
			break;
		char *kend = kcur + strcspn(kcur, " \t\n");
		char *vend = vcur + strcspn(vcur, " \t\n");
		if (*kend)
			*kend++ = '\0';
		if (*vend)
			*vend++ = '\0';
		keys[n] = kcur;
		vals[n] = vcur;
		++n;
		kcur = kend;
		vcur = vend;
	}

	for (size_t i = 0; i < n; ++i) {
		char stat[64];
		strlcpy(stat, keys[i], sizeof(stat));
		for (size_t j = 0; stat[j]; ++j)
			stat[j] = (char)tolower((unsigned char)stat[j]);
		uint64_t val = strtoull(vals[i], NULL, 10);
		metric_add_labels("ipvs_stat_total", &val, DATATYPE_UINT,
			ac->system_carg, "stat", stat);
	}
}

void get_snmp6_stats(void)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/net/snmp6", ac->system_procfs);
	carglog(ac->system_carg, L_TRACE, "system scrape metrics: network: snmp6 '%s'\n", path);

	FILE *fd = fopen(path, "r");
	if (!fd)
		return;

	char line[512];
	while (fgets(line, sizeof(line), fd)) {
		char key[128];
		int64_t val = 0;
		if (sscanf(line, "%127s %" SCNd64, key, &val) != 2)
			continue;
		char proto[64];
		char *stat = key;
		if (!strncmp(key, "Ip6", 3)) {
			strlcpy(proto, "Ip6", sizeof(proto));
			stat = key + 3;
		} else if (!strncmp(key, "Icmp6", 5)) {
			strlcpy(proto, "Icmp6", sizeof(proto));
			stat = key + 5;
		} else if (!strncmp(key, "Udp6", 4)) {
			strlcpy(proto, "Udp6", sizeof(proto));
			stat = key + 4;
		} else if (!strncmp(key, "UdpLite6", 8)) {
			strlcpy(proto, "UdpLite6", sizeof(proto));
			stat = key + 8;
		} else {
			strlcpy(proto, "snmp6", sizeof(proto));
		}
		metric_add_labels2("network_stat_total", &val, DATATYPE_INT, ac->system_carg,
			"proto", proto, "stat", stat);
	}
	fclose(fd);
}

void get_synproxy_stats(void)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/net/stat/synproxy", ac->system_procfs);
	carglog(ac->system_carg, L_TRACE, "system scrape metrics: network: synproxy '%s'\n", path);

	FILE *fd = fopen(path, "r");
	if (!fd)
		return;

	char header[512];
	char body[512];
	if (!fgets(header, sizeof(header), fd) || !fgets(body, sizeof(body), fd)) {
		fclose(fd);
		return;
	}

	char *hcur = header;
	char *bcur = body;
	hcur += strspn(hcur, " \t");
	bcur += strspn(bcur, " \t");
	while (*hcur && *hcur != '\n' && *bcur && *bcur != '\n') {
		char stat[64];
		size_t hlen = strcspn(hcur, " \t\n");
		if (!hlen)
			break;
		if (hlen >= sizeof(stat))
			hlen = sizeof(stat) - 1;
		strlcpy(stat, hcur, hlen + 1);
		uint64_t val = strtoull(bcur, NULL, 16);
		metric_add_labels("synproxy_stat_total", &val, DATATYPE_UINT, ac->system_carg, "stat", stat);
		hcur += hlen;
		hcur += strspn(hcur, " \t");
		bcur += strcspn(bcur, " \t\n");
		bcur += strspn(bcur, " \t");
	}
	fclose(fd);
}

void get_wireless_stats(void)
{
	if (!ac || !ac->system_procfs)
		return;

	char path[512];
	snprintf(path, sizeof(path), "%s/net/wireless", ac->system_procfs);
	carglog(ac->system_carg, L_TRACE, "system scrape metrics: network: wireless '%s'\n", path);

	FILE *fd = fopen(path, "r");
	if (!fd)
		return;

	char line[512];
	if (!fgets(line, sizeof(line), fd) || !fgets(line, sizeof(line), fd)) {
		fclose(fd);
		return;
	}

	while (fgets(line, sizeof(line), fd)) {
		char *fields[16];
		int n = 0;
		char *cur = line;
		while (n < 16) {
			cur += strspn(cur, " \t\n\r");
			if (!*cur)
				break;
			fields[n++] = cur;
			cur += strcspn(cur, " \t\n\r");
			if (*cur)
				*cur++ = '\0';
		}
		if (n < 11)
			continue;

		char *ifname = fields[0];
		size_t ilen = strlen(ifname);
		if (ilen && ifname[ilen - 1] == ':')
			ifname[ilen - 1] = '\0';
		if (!*ifname)
			continue;

		int64_t status = strtoll(fields[1], NULL, 16);
		int64_t link = atoll(fields[2]);
		int64_t level = atoll(fields[3]);
		int64_t noise = atoll(fields[4]);
		uint64_t nwid = strtoull(fields[5], NULL, 10);
		uint64_t crypt = strtoull(fields[6], NULL, 10);
		uint64_t frag = strtoull(fields[7], NULL, 10);
		uint64_t retry = strtoull(fields[8], NULL, 10);
		uint64_t misc = strtoull(fields[9], NULL, 10);
		uint64_t beacon = strtoull(fields[10], NULL, 10);

		metric_add_labels2("wireless_quality", &status, DATATYPE_INT,
			ac->system_carg, "ifname", ifname, "type", "status");
		metric_add_labels2("wireless_quality", &link, DATATYPE_INT,
			ac->system_carg, "ifname", ifname, "type", "link");
		metric_add_labels2("wireless_quality", &level, DATATYPE_INT,
			ac->system_carg, "ifname", ifname, "type", "level");
		metric_add_labels2("wireless_quality", &noise, DATATYPE_INT,
			ac->system_carg, "ifname", ifname, "type", "noise");
		metric_add_labels2("wireless_discarded_total", &nwid, DATATYPE_UINT,
			ac->system_carg, "ifname", ifname, "type", "nwid");
		metric_add_labels2("wireless_discarded_total", &crypt, DATATYPE_UINT,
			ac->system_carg, "ifname", ifname, "type", "crypt");
		metric_add_labels2("wireless_discarded_total", &frag, DATATYPE_UINT,
			ac->system_carg, "ifname", ifname, "type", "frag");
		metric_add_labels2("wireless_discarded_total", &retry, DATATYPE_UINT,
			ac->system_carg, "ifname", ifname, "type", "retry");
		metric_add_labels2("wireless_discarded_total", &misc, DATATYPE_UINT,
			ac->system_carg, "ifname", ifname, "type", "misc");
		metric_add_labels2("wireless_discarded_total", &beacon, DATATYPE_UINT,
			ac->system_carg, "ifname", ifname, "type", "beacon");
	}
	fclose(fd);
}

#endif
