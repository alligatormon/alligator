#ifdef _WIN64
#include <windows.h>
#include <stdio.h>
#include <psapi.h>
#pragma comment(lib, "user32.lib")


double get_process_name( DWORD processID )
{
    HANDLE hProcess;
    PROCESS_MEMORY_COUNTERS pmc;

    printf( "\nProcess ID: %u\n", processID );

    hProcess = OpenProcess(  PROCESS_QUERY_INFORMATION |
                                    PROCESS_VM_READ,
                                    FALSE, processID );
    if (NULL == hProcess)
        return;
        TCHAR Buffer[MAX_PATH];
        if (GetModuleFileNameEx(hProcess, 0, Buffer, MAX_PATH))
        {
            printf("name: %s\n", Buffer);
        }
        else
        {
            // You better call GetLastError() here
        }
        CloseHandle(hProcess);
}

double getCPUTime( DWORD processID )
{
    HANDLE hProcess;
    PROCESS_MEMORY_COUNTERS pmc;

    printf( "\nProcess ID: %u\n", processID );

    hProcess = OpenProcess(  PROCESS_QUERY_INFORMATION |
                                    PROCESS_VM_READ,
                                    FALSE, processID );
    if (NULL == hProcess)
        return;
    FILETIME createTime;
    FILETIME exitTime;
    FILETIME kernelTime;
    FILETIME userTime;
    if ( GetProcessTimes( hProcess, &createTime, &exitTime, &kernelTime, &userTime ) != -1 )
    {
        SYSTEMTIME userSystemTime;
        if ( FileTimeToSystemTime( &userTime, &userSystemTime ) != -1 )
            return (double)userSystemTime.wHour * 3600.0 +
                (double)userSystemTime.wMinute * 60.0 +
                (double)userSystemTime.wSecond +
                (double)userSystemTime.wMilliseconds / 1000.0;
    }
    return -1;
}


void PrintMemoryInfo( DWORD processID )
{
    HANDLE hProcess;
    PROCESS_MEMORY_COUNTERS pmc;

    // Print the process identifier.

    printf( "\nProcess ID: %u\n", processID );

    // Print information about the memory usage of the process.

    hProcess = OpenProcess(  PROCESS_QUERY_INFORMATION |
                                    PROCESS_VM_READ,
                                    FALSE, processID );
    if (NULL == hProcess)
        return;

    if ( GetProcessMemoryInfo( hProcess, &pmc, sizeof(pmc)) )
    {
        printf( "\tPageFaultCount: 0x%08X\n", pmc.PageFaultCount );
        printf( "\tPeakWorkingSetSize: 0x%08X\n", 
                  pmc.PeakWorkingSetSize );
        printf( "\tWorkingSetSize: 0x%08X\n", pmc.WorkingSetSize );
        printf( "\tQuotaPeakPagedPoolUsage: 0x%08X\n", 
                  pmc.QuotaPeakPagedPoolUsage );
        printf( "\tQuotaPagedPoolUsage: 0x%08X\n", 
                  pmc.QuotaPagedPoolUsage );
        printf( "\tQuotaPeakNonPagedPoolUsage: 0x%08X\n", 
                  pmc.QuotaPeakNonPagedPoolUsage );
        printf( "\tQuotaNonPagedPoolUsage: 0x%08X\n", 
                  pmc.QuotaNonPagedPoolUsage );
        printf( "\tPagefileUsage: 0x%08X\n", pmc.PagefileUsage ); 
        printf( "\tPeakPagefileUsage: 0x%08X\n", 
                  pmc.PeakPagefileUsage );
    }

    CloseHandle( hProcess );
}

void getprocessinfo()
{
 DWORD aProcesses[1024], cbNeeded, cProcesses;
    unsigned int i;

    if ( !EnumProcesses( aProcesses, sizeof(aProcesses), &cbNeeded ) )
    {
        return;
    }

    // Calculate how many process identifiers were returned.

    cProcesses = cbNeeded / sizeof(DWORD);

    // Print the memory usage for each process
    for ( i = 0; i < cProcesses; i++ )
    {
	get_process_name(aProcesses[i]);
        PrintMemoryInfo(aProcesses[i]);
    }

    double startTime[65535];
    uint32_t n;
    for ( i = 0; i < cProcesses; i++ )
    {
	startTime[i] = getCPUTime(aProcesses[i]);
    }
    n = i;
    Sleep(1000);
    for ( i = 0; i < n; i++ )
    {
	get_process_name(aProcesses[i]);
	double endTime = getCPUTime(aProcesses[i]);
	fprintf( stderr, "CPU time used = %lf\n", (endTime - startTime[i]) );

    }

    return;
}

void get_system_metrics()
{
	SYSTEM_INFO siSysInfo;

	// Copy the hardware information to the SYSTEM_INFO structure.

	GetSystemInfo(&siSysInfo);

	// Display the contents of the SYSTEM_INFO structure.

	printf("Hardware information: \n");
	printf("	Number of processors: %u\n",
		siSysInfo.dwNumberOfProcessors);
	printf("	Page size: %u\n", siSysInfo.dwPageSize);


	MEMORYSTATUSEX memInfo;
	memInfo.dwLength = sizeof(MEMORYSTATUSEX);
	GlobalMemoryStatusEx(&memInfo);
	DWORDLONG totalVirtualMem = memInfo.ullTotalPageFile;
	printf("totalVirtualMem %lld\n", totalVirtualMem);

	DWORDLONG virtualMemUsed = memInfo.ullTotalPageFile - memInfo.ullAvailPageFile;
	printf("virtualMemUsed %lld\n", virtualMemUsed);

	PROCESS_MEMORY_COUNTERS_EX pmc;
	GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
	SIZE_T virtualMemUsedByMe = pmc.PrivateUsage;
	printf("virtualMemUsedByMe %lld\n", virtualMemUsedByMe);

	DWORDLONG totalPhysMem = memInfo.ullTotalPhys;
	printf("totalPhysMem %lld\n", totalPhysMem);

	DWORDLONG physMemUsed = memInfo.ullTotalPhys - memInfo.ullAvailPhys;
	printf("physMemUsed %lld\n", physMemUsed);

	SIZE_T physMemUsedByMe = pmc.WorkingSetSize;
	printf("physMemUsedByMe %zu\n", physMemUsedByMe);

	getprocessinfo();
}
#endif
