#ifdef __linux__

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <main.h>
#include "common/xml.h"
#include "common/logs.h"
#define LIBVIRT_XML_SIZE 10240
/* Present in newer libvirt; hardcode for CentOS 7-era headers. */
#ifndef VIR_DOMAIN_MEMORY_STAT_DISK_CACHES
#define VIR_DOMAIN_MEMORY_STAT_DISK_CACHES 10
#endif

static void libvirt_log_domain_error(virDomainPtr d, const char *op)
{
	const char *dname = "?";
	unsigned int did = (unsigned int)-1;
	virErrorPtr err;

	if (d && ac->libvirt) {
		did = ac->libvirt->virDomainGetID(d);
		if (ac->libvirt->virDomainGetName(d))
			dname = ac->libvirt->virDomainGetName(d);
	}
	err = (ac->libvirt && ac->libvirt->virGetLastError)
	    ? ac->libvirt->virGetLastError() : NULL;
	carglog(ac->cadvisor_carg, L_ERROR,
	    "libvirt %s failed domain id=%u name=%s: %s\n",
	    op, did, dname,
	    (err && err->message) ? err->message : "unknown error");
}

static void libvirt_log_conn_error(const char *op)
{
	virErrorPtr err = (ac->libvirt && ac->libvirt->virGetLastError)
	    ? ac->libvirt->virGetLastError() : NULL;

	carglog(ac->cadvisor_carg, L_ERROR, "libvirt %s failed: %s\n", op,
	    (err && err->message) ? err->message : "unknown error");
}

static void libvirt_log_symbol_error(const char *symbol, const char *path)
{
	carglog(ac->cadvisor_carg, L_ERROR,
	    "Cannot load libvirt symbol '%s' from %s\n", symbol, path ? path : "?");
}

void libvirt_free(libvirt_library* libvirt) {
	if (libvirt->virDomainGetID_lib)
		free(libvirt->virDomainGetID_lib);

	if (libvirt->virDomainGetName_lib)
		free(libvirt->virDomainGetName_lib);

	if (libvirt->virDomainMemoryStats_lib)
		free(libvirt->virDomainMemoryStats_lib);

	if (libvirt->virDomainGetMaxMemory_lib)
		free(libvirt->virDomainGetMaxMemory_lib);

	if (libvirt->virDomainGetMaxVcpus_lib)
		free(libvirt->virDomainGetMaxVcpus_lib);

	if (libvirt->virDomainGetBlockIoTune_lib)
		free(libvirt->virDomainGetBlockIoTune_lib);

	if (libvirt->virDomainGetXMLDesc_lib)
		free(libvirt->virDomainGetXMLDesc_lib);

	if (libvirt->virDomainLookupByID_lib)
		free(libvirt->virDomainLookupByID_lib);

	if (libvirt->virConnectOpen_lib)
		free(libvirt->virConnectOpen_lib);

	if (libvirt->virConnectListAllDomains_lib)
		free(libvirt->virConnectListAllDomains_lib);

	if (libvirt->virDomainInterfaceStats_lib)
		free(libvirt->virDomainInterfaceStats_lib);

	if (libvirt->virDomainFree_lib)
		free(libvirt->virDomainFree_lib);

	if (libvirt->virConnectClose_lib)
		free(libvirt->virConnectClose_lib);

	if (libvirt->virGetLastError_lib)
		free(libvirt->virGetLastError_lib);

	free(libvirt);
}

libvirt_library* libvirt_init()
{
	module_t *mod = alligator_ht_search(ac->modules, module_compare, "libvirt", tommy_strhash_u32(0, "libvirt"));
	if (!mod)
	{
		carglog(ac->cadvisor_carg, L_TRACE, "No defined libvirt library in configuration\n");
		return NULL;
	}

	libvirt_library* libvirt = calloc(1, sizeof(*libvirt));

	libvirt->virDomainGetID = (unsigned int (*)(virDomainPtr))module_load(mod->path, "virDomainGetID", &libvirt->virDomainGetID_lib);
	if (!libvirt->virDomainGetID)
	{
		libvirt_log_symbol_error("virDomainGetID", mod->path);
		libvirt_free(libvirt);
		return NULL;
	}

	libvirt->virDomainGetName = (const char *(*)(virDomainPtr))module_load(mod->path, "virDomainGetName", &libvirt->virDomainGetName_lib);
	if (!libvirt->virDomainGetName)
	{
		libvirt_log_symbol_error("virDomainGetName", mod->path);
		libvirt_free(libvirt);
		return NULL;
	}

	libvirt->virDomainMemoryStats = (int (*)(virDomainPtr, virDomainMemoryStatPtr, unsigned int, unsigned int))module_load(mod->path, "virDomainMemoryStats", &libvirt->virDomainMemoryStats_lib);
	if (!libvirt->virDomainMemoryStats)
	{
		libvirt_log_symbol_error("virDomainMemoryStats", mod->path);
		libvirt_free(libvirt);
		return NULL;
	}

	libvirt->virDomainSetMemoryStatsPeriod = (int (*)(virDomainPtr, int, unsigned int))module_load(mod->path, "virDomainSetMemoryStatsPeriod", &libvirt->virDomainSetMemoryStatsPeriod_lib);
	if (!libvirt->virDomainSetMemoryStatsPeriod)
	{
		libvirt_log_symbol_error("virDomainSetMemoryStatsPeriod", mod->path);
		libvirt_free(libvirt);
		return NULL;
	}

	libvirt->virDomainGetMaxMemory = (unsigned long (*)(virDomainPtr))module_load(mod->path, "virDomainGetMaxMemory", &libvirt->virDomainGetMaxMemory_lib);
	if (!libvirt->virDomainGetMaxMemory_lib)
	{
		libvirt_log_symbol_error("virDomainGetMaxMemory", mod->path);
		libvirt_free(libvirt);
		return NULL;
	}

	libvirt->virDomainGetMaxVcpus = (int (*)(virDomainPtr))module_load(mod->path, "virDomainGetMaxVcpus", &libvirt->virDomainGetMaxVcpus_lib);
	if (!libvirt->virDomainGetMaxVcpus)
	{
		libvirt_log_symbol_error("virDomainGetMaxVcpus", mod->path);
		libvirt_free(libvirt);
		return NULL;
	}

	libvirt->virDomainGetBlockIoTune = (int (*)(virDomainPtr, const char *, virTypedParameterPtr, int *, unsigned int))module_load(mod->path, "virDomainGetBlockIoTune", &libvirt->virDomainGetBlockIoTune_lib);
	if (!libvirt->virDomainGetBlockIoTune_lib)
	{
		libvirt_log_symbol_error("virDomainGetBlockIoTune", mod->path);
		libvirt_free(libvirt);
		return NULL;
	}

	libvirt->virDomainGetXMLDesc = (char *(*)(virDomainPtr, unsigned int))module_load(mod->path, "virDomainGetXMLDesc", &libvirt->virDomainGetXMLDesc_lib);
	if (!libvirt->virDomainGetXMLDesc)
	{
		libvirt_log_symbol_error("virDomainGetXMLDesc", mod->path);
		libvirt_free(libvirt);
		return NULL;
	}

	libvirt->virDomainLookupByID = (virDomainPtr (*)(virConnectPtr, int))module_load(mod->path, "virDomainLookupByID", &libvirt->virDomainLookupByID_lib);
	if (!libvirt->virDomainLookupByID)
	{
		libvirt_log_symbol_error("virDomainLookupByID", mod->path);
		libvirt_free(libvirt);
		return NULL;
	}

	libvirt->virConnectOpen = (virConnectPtr (*)(char *))module_load(mod->path, "virConnectOpen", &libvirt->virConnectOpen_lib);
	if (!libvirt->virConnectOpen)
	{
		libvirt_log_symbol_error("virConnectOpen", mod->path);
		libvirt_free(libvirt);
		return NULL;
	}

	libvirt->virConnectListAllDomains = (int (*)(virConnectPtr, virDomainPtr **, unsigned int))module_load(mod->path, "virConnectListAllDomains", &libvirt->virConnectListAllDomains_lib);
	if (!libvirt->virConnectListAllDomains)
	{
		libvirt_log_symbol_error("virConnectListAllDomains", mod->path);
		libvirt_free(libvirt);
		return NULL;
	}

	libvirt->virDomainInterfaceStats = (int (*)(virDomainPtr, char *, virDomainInterfaceStatsPtr, size_t))module_load(mod->path, "virDomainInterfaceStats", &libvirt->virDomainInterfaceStats_lib);
	if (!libvirt->virDomainInterfaceStats)
	{
		libvirt_log_symbol_error("virDomainInterfaceStats", mod->path);
		libvirt_free(libvirt);
		return NULL;
	}

	libvirt->virDomainFree = (int (*)(virDomainPtr))module_load(mod->path, "virDomainFree", &libvirt->virDomainFree_lib);
	if (!libvirt->virDomainFree)
	{
		libvirt_log_symbol_error("virDomainFree", mod->path);
		libvirt_free(libvirt);
		return NULL;
	}

	libvirt->virConnectClose = (int (*)(virConnectPtr))module_load(mod->path, "virConnectClose", &libvirt->virConnectClose_lib);
	if (!libvirt->virConnectClose)
	{
		libvirt_log_symbol_error("virConnectClose", mod->path);
		libvirt_free(libvirt);
		return NULL;
	}

	libvirt->virGetLastError = (virErrorPtr (*)(void))module_load(mod->path, "virGetLastError", &libvirt->virGetLastError_lib);
	if (!libvirt->virGetLastError)
	{
		libvirt_log_symbol_error("virGetLastError", mod->path);
		libvirt_free(libvirt);
		return NULL;
	}

	return libvirt;
}

void libvirt_if_dev_stats(virDomainPtr d, char *id, char *name, char *ifname) {
	virDomainInterfaceStatsStruct stats = {0};
	if (ac->libvirt->virDomainInterfaceStats(d, ifname, &stats, sizeof(stats)) != 0) {
		libvirt_log_domain_error(d, "virDomainInterfaceStats");
		return;
	}

	carglog(ac->cadvisor_carg, L_DEBUG, "libvirt_if_dev_stats '%s' '%s' '%s': rx_bytes %lld, tx_bytes %lld, rx_packets %lld, tx_packets %lld, rx_errors %lld, tx_errors %lld, rx_drop %lld, tx_drop %lld\n", name, id, ifname, stats.rx_bytes, stats.tx_bytes, stats.rx_packets, stats.tx_packets, stats.rx_errs, stats.tx_errs, stats.rx_drop, stats.tx_drop);
	metric_add_labels4("container_network_receive_bytes_total", &stats.rx_bytes, DATATYPE_INT, ac->cadvisor_carg, "name", name, "id", id, "libvirt_id", id, "interface", ifname);
	metric_add_labels4("container_network_transmit_bytes_total", &stats.tx_bytes, DATATYPE_INT, ac->cadvisor_carg, "name", name, "id", id, "libvirt_id", id, "interface", ifname);
	metric_add_labels4("container_network_receive_packets_total", &stats.rx_packets, DATATYPE_INT, ac->cadvisor_carg, "name", name, "id", id, "libvirt_id", id, "interface", ifname);
	metric_add_labels4("container_network_transmit_packets_total", &stats.tx_packets, DATATYPE_INT, ac->cadvisor_carg, "name", name, "id", id, "libvirt_id", id, "interface", ifname);
	metric_add_labels4("container_network_receive_errors_total", &stats.rx_errs, DATATYPE_INT, ac->cadvisor_carg, "name", name, "id", id, "libvirt_id", id, "interface", ifname);
	metric_add_labels4("container_network_transmit_errors_total", &stats.tx_errs, DATATYPE_INT, ac->cadvisor_carg, "name", name, "id", id, "libvirt_id", id, "interface", ifname);
	metric_add_labels4("container_network_receive_packets_dropped_total", &stats.rx_drop, DATATYPE_INT, ac->cadvisor_carg, "name", name, "id", id, "libvirt_id", id, "interface", ifname);
	metric_add_labels4("container_network_transmit_packets_dropped_total", &stats.tx_drop, DATATYPE_INT, ac->cadvisor_carg, "name", name, "id", id, "libvirt_id", id, "interface", ifname);
}

void libvirt_ifstat(virDomainPtr d, char *id, char *name) {
	char *xml = ac->libvirt->virDomainGetXMLDesc(d, 0);
	char *tmp = xml;
	size_t xml_len = strlen(xml);
	char xmlname[LIBVIRT_XML_SIZE];
	char xmlvalue[LIBVIRT_XML_SIZE];
	size_t xmlname_size = LIBVIRT_XML_SIZE;
	size_t xmlvalue_size = LIBVIRT_XML_SIZE;
	size_t end;
	while (tmp++ - xml < xml_len)
	{
		tmp = strstr(tmp, "<interface");

		if (!tmp)
			continue;

		if (!get_xml_node(tmp, (xml_len - (tmp - xml)), xmlname, &xmlname_size, xmlvalue, &xmlvalue_size, &end))
			continue;

		char *nodetmp = xmlvalue;

		nodetmp = strstr(nodetmp, "<target");
		if (!nodetmp)
			continue;

		nodetmp = strstr(nodetmp, "dev=");
		if (!nodetmp)
			continue;

		nodetmp += 5;
		char devname[128];

		strlcpy(devname, nodetmp, strcspn(nodetmp, "'\" />")+1);
		libvirt_if_dev_stats(d, id, name, devname);

		tmp += end;
	}

	free(xml);
}

void libvirt_get_blkio_info(virDomainPtr d, char *id, char *name, char *devname) {

	virTypedParameterPtr params = NULL;
	int nparams = 0;
	int ret = ac->libvirt->virDomainGetBlockIoTune(d, devname, NULL, &nparams, 0);
	if (ret) {
		libvirt_log_domain_error(d, "virDomainGetBlockIoTune");
		return;
	}
	params = calloc(nparams, sizeof(*params));
	ret = ac->libvirt->virDomainGetBlockIoTune(d, devname, params, &nparams, 0);
	if (ret) {
		free(params);
		libvirt_log_domain_error(d, "virDomainGetBlockIoTune");
		return;
	}

	for (int i = 0; i < nparams; i++) {
		uint64_t ioval = 0;
		char *ptr;
		ptr = params[i].field;
		switch (params[i].type) {
			case VIR_TYPED_PARAM_INT:
				ioval = params[i].value.i;
				break;
			case VIR_TYPED_PARAM_UINT:
				ioval = params[i].value.ui;
				break;
			case VIR_TYPED_PARAM_LLONG:
				ioval = params[i].value.l;
				break;
			case VIR_TYPED_PARAM_ULLONG:
				ioval = params[i].value.ul;
				break;
			case VIR_TYPED_PARAM_DOUBLE:
				ioval = params[i].value.d;
				break;
			case VIR_TYPED_PARAM_BOOLEAN:
				ioval = params[i].value.b;
				break;
			case VIR_TYPED_PARAM_STRING:
				free(params[i].value.s);
				break;
			default:
				break;
		}

		//printf("id: %s, name: '%s', dev: '%s', param: '%s', val: '%lu'\n", id, name, devname, ptr, ioval);
		if (ioval) {
			if (!strcmp(ptr, "total_bytes_sec"))
				metric_add_labels4("container_spec_fs_total_rate_limit_bytes", &ioval, DATATYPE_UINT, ac->cadvisor_carg, "name", name, "id", id, "libvirt_id", id, "device", devname);

			else if (!strcmp(ptr, "read_iops_sec"))
				metric_add_labels4("container_spec_fs_reads_rate_limit_bytes", &ioval, DATATYPE_UINT, ac->cadvisor_carg, "name", name, "id", id, "libvirt_id", id, "device", devname);

			else if (!strcmp(ptr, "write_bytes_sec"))
				metric_add_labels4("container_spec_fs_writes_rate_limit_bytes", &ioval, DATATYPE_UINT, ac->cadvisor_carg, "name", name, "id", id, "libvirt_id", id, "device", devname);

			else if (!strcmp(ptr, "total_iops_sec"))
				metric_add_labels4("container_spec_fs_total_rate_limit_total", &ioval, DATATYPE_UINT, ac->cadvisor_carg, "name", name, "id", id, "libvirt_id", id, "device", devname);

			else if (!strcmp(ptr, "read_iops_sec"))
				metric_add_labels4("container_spec_fs_reads_rate_limit_total", &ioval, DATATYPE_UINT, ac->cadvisor_carg, "name", name, "id", id, "libvirt_id", id, "device", devname);

			else if (!strcmp(ptr, "write_iops_sec"))
				metric_add_labels4("container_spec_fs_writes_rate_limit_total", &ioval, DATATYPE_UINT, ac->cadvisor_carg, "name", name, "id", id, "libvirt_id", id, "device", devname);
		}
	}

	free(params);
}

void libvirt_memory(virDomainPtr d, char* id, char *name) {
	/* Fixed size 13: old headers may define VIR_DOMAIN_MEMORY_STAT_NR < 13. */
	virDomainMemoryStatStruct memstats[13];
	memset(memstats, 0, sizeof(memstats));
	int rc = ac->libvirt->virDomainSetMemoryStatsPeriod(d, 10, VIR_DOMAIN_AFFECT_LIVE);
	if (rc < 0)
		libvirt_log_domain_error(d, "virDomainSetMemoryStatsPeriod");

	int nstats = ac->libvirt->virDomainMemoryStats(d, memstats, 13, 0);
	if (nstats < 0) {
		libvirt_log_domain_error(d, "virDomainMemoryStats");
		nstats = 0;
	}

	/* virDomainMemoryStats packs available tags densely; match by .tag, not index. */
	uint64_t rssmemory = 0;
	uint64_t freememory = 0;
	uint64_t inactive_file = 0;
	for (int i = 0; i < nstats; i++) {
		uint64_t val = memstats[i].val * 1024;
		switch (memstats[i].tag) {
			case VIR_DOMAIN_MEMORY_STAT_RSS:
				rssmemory = val;
				break;
			case VIR_DOMAIN_MEMORY_STAT_UNUSED:
				freememory = val;
				break;
			case VIR_DOMAIN_MEMORY_STAT_DISK_CACHES:
				inactive_file = val;
				break;
			default:
				break;
		}
	}

	metric_add_labels3("container_memory_rss", &rssmemory, DATATYPE_UINT, ac->cadvisor_carg, "name", name, "id", id, "libvirt_id", id);
	metric_add_labels3("container_memory_cache", &inactive_file, DATATYPE_UINT, ac->cadvisor_carg, "name", name, "id", id, "libvirt_id", id);

	uint64_t maxmemory = (uint64_t)ac->libvirt->virDomainGetMaxMemory(d) * 1024;
	if (!maxmemory) {
		libvirt_log_domain_error(d, "virDomainGetMaxMemory");
		return;
	}

	metric_add_labels3("container_spec_memory_swap_limit_bytes", &maxmemory, DATATYPE_UINT, ac->cadvisor_carg, "name", name, "id", id, "libvirt_id", id);
	metric_add_labels3("container_spec_memory_limit_bytes", &maxmemory, DATATYPE_UINT, ac->cadvisor_carg, "name", name, "id", id, "libvirt_id", id);

	uint64_t usedmemory = (freememory < maxmemory) ? (maxmemory - freememory) : 0;
	metric_add_labels3("container_memory_usage_bytes", &usedmemory, DATATYPE_UINT, ac->cadvisor_carg, "name", name, "id", id, "libvirt_id", id);
}

void libvirt_cpu(virDomainPtr d, char* id, char *name) {
	uint64_t maxcpus = ac->libvirt->virDomainGetMaxVcpus(d);
	//printf("name of domain %d is %s maxcpus %d\n", id, name, maxcpus);
	metric_add_labels3("container_spec_cpu_shares", &maxcpus, DATATYPE_UINT, ac->cadvisor_carg, "name", name, "id", id, "libvirt_id", id);
}

void libvirt_blkio(virDomainPtr d, char *id, char *name) {
	char *xml = ac->libvirt->virDomainGetXMLDesc(d, 0);
	char *tmp = xml;
	size_t xml_len = strlen(xml);
	char xmlname[LIBVIRT_XML_SIZE];
	char xmlvalue[LIBVIRT_XML_SIZE];
	size_t xmlname_size = LIBVIRT_XML_SIZE;
	size_t xmlvalue_size = LIBVIRT_XML_SIZE;
	size_t end;
	while (tmp++ - xml < xml_len)
	{
		tmp = strstr(tmp, "<disk");

		if (!tmp)
			continue;

		if (!get_xml_node(tmp, (xml_len - (tmp - xml)), xmlname, &xmlname_size, xmlvalue, &xmlvalue_size, &end))
			continue;

		char *nodetmp = xmlvalue;

		nodetmp = strstr(nodetmp, "<target");
		if (!nodetmp)
			continue;

		nodetmp = strstr(nodetmp, "dev=");
		if (!nodetmp)
			continue;

		nodetmp += 5;
		char devname[128];

		strlcpy(devname, nodetmp, strcspn(nodetmp, "'\" />")+1);
		libvirt_get_blkio_info(d, id, name, devname);

		tmp += end;
	}

	free(xml);
}

void scrape_domain(virDomainPtr d) {
	int id = ac->libvirt->virDomainGetID(d);
	char cid[19];
	snprintf(cid, 18, "%d", id);
	char *name = (char*)ac->libvirt->virDomainGetName(d);

	libvirt_memory(d, cid, name);
	libvirt_cpu(d, cid, name);
	libvirt_blkio(d, cid, name);
	libvirt_ifstat(d, cid, name);
}

void get_memory(virConnectPtr c, int i) {
	virDomainPtr d = ac->libvirt->virDomainLookupByID(c, i);
	scrape_domain(d);
}


int libvirt() {
	if (!ac->libvirt) {
		ac->libvirt = libvirt_init();
		if (!ac->libvirt)
			return 0;
	}

	virConnectPtr conn = ac->libvirt->virConnectOpen(NULL);
	if (conn == NULL) {
		libvirt_log_conn_error("virConnectOpen");
		return 0;
	}

	virDomainPtr *domains;
	size_t domains_size = 0;
	int ret = ac->libvirt->virConnectListAllDomains(conn, &domains, VIR_CONNECT_LIST_DOMAINS_RUNNING);
	if (ret > -1) {
		for (domains_size = 0; domains_size < ret; ++domains_size) {
			 scrape_domain(domains[domains_size]);
			 ac->libvirt->virDomainFree(domains[domains_size]);
		}
		free(domains);
	} else {
		libvirt_log_conn_error("virConnectListAllDomains");
	}

	//get_memory(conn, 1);
	//get_memory(conn, 2);
	//get_memory(conn, 3);

	ac->libvirt->virConnectClose(conn);
	return domains_size;
}

#endif /* __linux__ */
