#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <main.h>
#include "common/xml.h"
#include "common/logs.h"
#define LIBVIRT_XML_SIZE 10240

void libvirt_free(libvirt_library* libvirt) {
	if (libvirt->virDomainGetID)
		free(libvirt->virDomainGetID);

	if (libvirt->virDomainGetName)
		free(libvirt->virDomainGetName);

	if (libvirt->virDomainMemoryStats)
		free(libvirt->virDomainMemoryStats);

	if (libvirt->virDomainGetMaxMemory_lib)
		free(libvirt->virDomainGetMaxMemory_lib);

	if (libvirt->virDomainGetMaxVcpus)
		free(libvirt->virDomainGetMaxVcpus);

	if (libvirt->virDomainGetBlockIoTune_lib)
		free(libvirt->virDomainGetBlockIoTune_lib);

	if (libvirt->virDomainGetXMLDesc)
		free(libvirt->virDomainGetXMLDesc);

	if (libvirt->virDomainLookupByID)
		free(libvirt->virDomainLookupByID);

	if (libvirt->virConnectOpen)
		free(libvirt->virConnectOpen);

	if (libvirt->virConnectListAllDomains)
		free(libvirt->virConnectListAllDomains);

	if (libvirt->virDomainFree)
		free(libvirt->virDomainFree);

	if (libvirt->virConnectClose)
		free(libvirt->virConnectClose);

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
		carglog(ac->cadvisor_carg, L_ERROR, "Cannot get virDomainGetID from libvirt\n");
		libvirt_free(libvirt);
		return NULL;
	}

	libvirt->virDomainGetName = (const char *(*)(virDomainPtr))module_load(mod->path, "virDomainGetName", &libvirt->virDomainGetName_lib);
	if (!libvirt->virDomainGetName)
	{
		carglog(ac->cadvisor_carg, L_ERROR, "Cannot get virDomainGetName from libvirt\n");
		libvirt_free(libvirt);
		return NULL;
	}

	libvirt->virDomainMemoryStats = (int (*)(virDomainPtr, virDomainMemoryStatPtr, unsigned int, unsigned int))module_load(mod->path, "virDomainMemoryStats", &libvirt->virDomainMemoryStats_lib);
	if (!libvirt->virDomainMemoryStats)
	{
		carglog(ac->cadvisor_carg, L_ERROR, "Cannot get virDomainMemoryStats from libvirt\n");
		libvirt_free(libvirt);
		return NULL;
	}

	libvirt->virDomainGetMaxMemory = (unsigned long (*)(virDomainPtr))module_load(mod->path, "virDomainGetMaxMemory", &libvirt->virDomainGetMaxMemory_lib);
	if (!libvirt->virDomainGetMaxMemory_lib)
	{
		carglog(ac->cadvisor_carg, L_ERROR, "Cannot get virDomainGetMaxMemory_lib from libvirt\n");
		libvirt_free(libvirt);
		return NULL;
	}

	libvirt->virDomainGetMaxVcpus = (int (*)(virDomainPtr))module_load(mod->path, "virDomainGetMaxVcpus", &libvirt->virDomainGetMaxVcpus_lib);
	if (!libvirt->virDomainGetMaxVcpus)
	{
		carglog(ac->cadvisor_carg, L_ERROR, "Cannot get virDomainGetMaxVcpus from libvirt\n");
		libvirt_free(libvirt);
		return NULL;
	}

	libvirt->virDomainGetBlockIoTune = (int (*)(virDomainPtr, const char *, virTypedParameterPtr, int *, unsigned int))module_load(mod->path, "virDomainGetBlockIoTune", &libvirt->virDomainGetBlockIoTune_lib);
	if (!libvirt->virDomainGetBlockIoTune_lib)
	{
		carglog(ac->cadvisor_carg, L_ERROR, "Cannot get virDomainGetBlockIoTune_lib from libvirt\n");
		libvirt_free(libvirt);
		return NULL;
	}

	libvirt->virDomainGetXMLDesc = (char *(*)(virDomainPtr, unsigned int))module_load(mod->path, "virDomainGetXMLDesc", &libvirt->virDomainGetXMLDesc_lib);
	if (!libvirt->virDomainGetXMLDesc)
	{
		carglog(ac->cadvisor_carg, L_ERROR, "Cannot get virDomainGetXMLDesc from libvirt\n");
		libvirt_free(libvirt);
		return NULL;
	}

	libvirt->virDomainLookupByID = (virDomainPtr (*)(virConnectPtr, int))module_load(mod->path, "virDomainLookupByID", &libvirt->virDomainLookupByID_lib);
	if (!libvirt->virDomainLookupByID)
	{
		carglog(ac->cadvisor_carg, L_ERROR, "Cannot get virDomainLookupByID from libvirt\n");
		libvirt_free(libvirt);
		return NULL;
	}

	libvirt->virConnectOpen = (virConnectPtr (*)(char *))module_load(mod->path, "virConnectOpen", &libvirt->virConnectOpen_lib);
	if (!libvirt->virConnectOpen)
	{
		carglog(ac->cadvisor_carg, L_ERROR, "Cannot get virConnectOpen from libvirt\n");
		libvirt_free(libvirt);
		return NULL;
	}

	libvirt->virConnectListAllDomains = (int (*)(virConnectPtr, virDomainPtr **, unsigned int))module_load(mod->path, "virConnectListAllDomains", &libvirt->virConnectListAllDomains_lib);
	if (!libvirt->virConnectListAllDomains)
	{
		carglog(ac->cadvisor_carg, L_ERROR, "Cannot get virConnectListAllDomains from libvirt\n");
		libvirt_free(libvirt);
		return NULL;
	}

	libvirt->virDomainFree = (int (*)(virDomainPtr))module_load(mod->path, "virDomainFree", &libvirt->virDomainFree_lib);
	if (!libvirt->virDomainFree)
	{
		carglog(ac->cadvisor_carg, L_ERROR, "Cannot get virDomainFree from libvirt\n");
		libvirt_free(libvirt);
		return NULL;
	}

	libvirt->virConnectClose = (int (*)(virConnectPtr))module_load(mod->path, "virConnectClose", &libvirt->virConnectClose_lib);
	if (!libvirt->virConnectClose)
	{
		carglog(ac->cadvisor_carg, L_ERROR, "Cannot get virConnectClose from libvirt\n");
		libvirt_free(libvirt);
		return NULL;
	}

	return libvirt;
}

void libvirt_get_blkio_info(virDomainPtr d, char *id, char *name, char *devname) {

	virTypedParameterPtr params = NULL;
	int nparams = 0;
	int ret = ac->libvirt->virDomainGetBlockIoTune(d, devname, NULL, &nparams, 0);
	if (ret)
		return;
	params = calloc(nparams, sizeof(*params));
	ret = ac->libvirt->virDomainGetBlockIoTune(d, devname, params, &nparams, 0);

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
	virDomainMemoryStatStruct memstats[VIR_DOMAIN_MEMORY_STAT_NR];
	memset(memstats, 0, sizeof(virDomainMemoryStatStruct) * VIR_DOMAIN_MEMORY_STAT_NR);
	ac->libvirt->virDomainMemoryStats(d, (virDomainMemoryStatPtr)&memstats, VIR_DOMAIN_MEMORY_STAT_NR, 0);

	uint64_t maxmemory = ac->libvirt->virDomainGetMaxMemory(d) * 1024;
	metric_add_labels3("container_spec_memory_swap_limit_bytes", &maxmemory, DATATYPE_UINT, ac->cadvisor_carg, "name", name, "id", id, "libvirt_id", id);
	metric_add_labels3("container_spec_memory_limit_bytes", &maxmemory, DATATYPE_UINT, ac->cadvisor_carg, "name", name, "id", id, "libvirt_id", id);

	uint64_t rssmemory = memstats[VIR_DOMAIN_MEMORY_STAT_RSS].val * 1024;
	metric_add_labels3("container_memory_rss", &rssmemory, DATATYPE_UINT, ac->cadvisor_carg, "name", name, "id", id, "libvirt_id", id);

	//unsigned long availmemory = memstats[VIR_DOMAIN_MEMORY_STAT_AVAILABLE].val * 1024;
	unsigned long freememory = memstats[VIR_DOMAIN_MEMORY_STAT_UNUSED].val * 1024;

	uint64_t inactive_file = memstats[VIR_DOMAIN_MEMORY_STAT_DISK_CACHES].val * 1024;
	metric_add_labels3("container_memory_cache", &inactive_file, DATATYPE_UINT, ac->cadvisor_carg, "name", name, "id", id, "libvirt_id", id);

	uint64_t usedmemory = maxmemory - freememory;
	metric_add_labels3("container_memory_usage_bytes", &usedmemory, DATATYPE_UINT, ac->cadvisor_carg, "name", name, "id", id, "libvirt_id", id);
	//printf("name of domain %d is %s with memory rss %lu/used %lu/avail %lu/free %lu/ inactive %lu/max %lu\n", id, name, rssmemory/1048576, usedmemory/1048576, availmemory/1048576, freememory/1048576, inactive_file/1048576, maxmemory);
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

}

void get_memory(virConnectPtr c, int i) {
	virDomainPtr d = ac->libvirt->virDomainLookupByID(c, i);
	scrape_domain(d);
}


int libvirt(int argc, char **argv) {
	if (!ac->libvirt) {
		ac->libvirt = libvirt_init();
		if (!ac->libvirt)
			return 0;
	}

	virConnectPtr conn = ac->libvirt->virConnectOpen(NULL);
	if (conn == NULL) {
		fprintf(stderr, "Failed to connect to hypervisor\n");
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
	}

	//get_memory(conn, 1);
	//get_memory(conn, 2);
	//get_memory(conn, 3);

	ac->libvirt->virConnectClose(conn);
	return domains_size;
}