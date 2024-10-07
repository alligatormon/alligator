#pragma once
#include <libvirt/libvirt.h>
typedef struct libvirt_library {
	uv_lib_t *virDomainGetID_lib;
	unsigned int (*virDomainGetID)(virDomainPtr domain);

	uv_lib_t *virDomainGetName_lib;
	const char * (*virDomainGetName)(virDomainPtr domain);

	uv_lib_t *virDomainMemoryStats_lib;
	int (*virDomainMemoryStats)(virDomainPtr, virDomainMemoryStatPtr, unsigned int, unsigned int);

	uv_lib_t *virDomainGetMaxMemory_lib;
	unsigned long (*virDomainGetMaxMemory)(virDomainPtr domain);

	uv_lib_t *virDomainGetMaxVcpus_lib;
	int (*virDomainGetMaxVcpus)(virDomainPtr domain);

	uv_lib_t *virDomainGetBlockIoTune_lib;
	int (*virDomainGetBlockIoTune)(virDomainPtr domain, const char*, virTypedParameterPtr, int*, unsigned int);

	uv_lib_t *virDomainGetXMLDesc_lib;
	char* (*virDomainGetXMLDesc)(virDomainPtr domain, unsigned int);

	uv_lib_t *virDomainLookupByID_lib;
	virDomainPtr (*virDomainLookupByID)(virConnectPtr, int);

	uv_lib_t *virConnectOpen_lib;
	virConnectPtr (*virConnectOpen)(char*);

	uv_lib_t *virConnectListAllDomains_lib;
	int (*virConnectListAllDomains)(virConnectPtr, virDomainPtr **, unsigned int);

	uv_lib_t *virDomainInterfaceStats_lib;
	int (*virDomainInterfaceStats)(virDomainPtr, char*, virDomainInterfaceStatsPtr, size_t);

	uv_lib_t *virDomainFree_lib;
	int (*virDomainFree)(virDomainPtr domain);

	uv_lib_t *virConnectClose_lib;
	int (*virConnectClose)(virConnectPtr);
} libvirt_library;

int libvirt();
