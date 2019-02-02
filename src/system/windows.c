#ifdef _WIN64
#include <windows.h>
#include <stdio.h>
#include <psapi.h>
#pragma comment(lib, "user32.lib")
void get_system_metrics()
{
	SYSTEM_INFO siSysInfo;

	// Copy the hardware information to the SYSTEM_INFO structure.

	GetSystemInfo(&siSysInfo);

	// Display the contents of the SYSTEM_INFO structure.

	printf("Hardware information: \n");
	printf("	OEM ID: %u\n", siSysInfo.dwOemId);
	printf("	Number of processors: %u\n",
		siSysInfo.dwNumberOfProcessors);
	printf("	Page size: %u\n", siSysInfo.dwPageSize);
	printf("	Processor type: %u\n", siSysInfo.dwProcessorType);
	printf("	Minimum application address: %lx\n",
		siSysInfo.lpMinimumApplicationAddress);
	printf("	Maximum application address: %lx\n",
		siSysInfo.lpMaximumApplicationAddress);
	printf("	Active processor mask: %u\n",
		siSysInfo.dwActiveProcessorMask);


	MEMORYSTATUSEX memInfo;
	memInfo.dwLength = sizeof(MEMORYSTATUSEX);
	GlobalMemoryStatusEx(&memInfo);
	DWORDLONG totalVirtualMem = memInfo.ullTotalPageFile;

	DWORDLONG virtualMemUsed = memInfo.ullTotalPageFile - memInfo.ullAvailPageFile;

	PROCESS_MEMORY_COUNTERS_EX pmc;
	GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
	SIZE_T virtualMemUsedByMe = pmc.PrivateUsage;

	DWORDLONG totalPhysMem = memInfo.ullTotalPhys;

	DWORDLONG physMemUsed = memInfo.ullTotalPhys - memInfo.ullAvailPhys;

	SIZE_T physMemUsedByMe = pmc.WorkingSetSize;
}
#endif
