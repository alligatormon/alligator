#ifdef _WIN64
#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

#define MALLOC(x) HeapAlloc(GetProcessHeap(), 0, (x))
#define FREE(x) HeapFree(GetProcessHeap(), 0, (x))

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
    CloseHandle(hProcess);
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
        printf( "\tPageFaultCount: %lu\n", pmc.PageFaultCount );
        printf( "\tPeakWorkingSetSize: %lu\n", 
                  pmc.PeakWorkingSetSize );
        printf( "\tWorkingSetSize: %lu\n", pmc.WorkingSetSize );
        printf( "\tQuotaPeakPagedPoolUsage: %lu\n", 
                  pmc.QuotaPeakPagedPoolUsage );
        printf( "\tQuotaPagedPoolUsage: %lu\n", 
                  pmc.QuotaPagedPoolUsage );
        printf( "\tQuotaPeakNonPagedPoolUsage: %lu\n", 
                  pmc.QuotaPeakNonPagedPoolUsage );
        printf( "\tQuotaNonPagedPoolUsage: %lu\n", 
                  pmc.QuotaNonPagedPoolUsage );
        printf( "\tPagefileUsage: %lu\n", pmc.PagefileUsage ); 
        printf( "\tPeakPagefileUsage: %lu\n", 
                  pmc.PeakPagefileUsage );
    }

    CloseHandle( hProcess );
}

void descriptors_info( DWORD processID )
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

      DWORD type_char = 0, 
      type_disk = 0, 
      type_pipe = 0, 
      type_remote = 0, 
      type_unknown = 0,
      handles_count = 0;

    GetProcessHandleCount(GetCurrentProcess(), &handles_count);
    handles_count *= 4;


    for (DWORD handle = 0x4; handle < handles_count; handle += 4) {
        switch (GetFileType((HANDLE)handle)){
            case FILE_TYPE_CHAR:
                type_char++;
                break;
            case FILE_TYPE_DISK:
                type_disk++;
                break;
            case FILE_TYPE_PIPE: 
                type_pipe++;
                break;
            case FILE_TYPE_REMOTE: 
                type_remote++;
                break;
            case FILE_TYPE_UNKNOWN:
                if (GetLastError() == NO_ERROR) type_unknown++;
                break;

        }
    }
    printf("char devices %lu\n", type_char);
    printf("disk devices %lu\n", type_disk);
    printf("pipe devices %lu\n", type_pipe);
    printf("remote devices %lu\n", type_remote);
    printf("unknown devices %lu\n", type_unknown);

    CloseHandle( hProcess );
}

DWORD EnumerateThreads(DWORD pid)
{
    char szText[MAX_PATH];

    static BOOL bStarted;
    static HANDLE hSnapPro, hSnapThread;
    static LPPROCESSENTRY32 ppe32;
    static PTHREADENTRY32 pte32;


    if (!bStarted)
    {
        if (!bStarted)
        {
            bStarted++;
            pte32 = (THREADENTRY32*) MALLOC(THREADENTRY32);
            pte32->dwSize = sizeof(THREADENTRY32);

            hSnapThread = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);

            if (!hSnapThread)
            {
                FormatErrorMessage("GetLastError -> hSnapThread = CreateToolhelp32Snapshot\n", GetLastError());
                FREE(pte32);
                bStarted = 0;
                return 0;
            }

            if (Thread32First(hSnapThread, pte32))
            {
                do
                {
                    if (pid == pte32->th32OwnerProcessID)
                    {
                        wsprintf(szText, "__yes Thread32First pid: 0x%X - tid: 0x%X\n", pid, pte32->th32ThreadID);
                        OutputDebugString(szText);
                        return pte32->th32ThreadID;
                    }
                } 
                while (Thread32Next(hSnapThread, pte32));
            }
            else
                FormatErrorMessage("GetLastError ->Thread32First\n", GetLastError());
        }
    }

    if (Thread32Next(hSnapThread, pte32))
    {
        do
        {
            if (pid == pte32->th32OwnerProcessID)
            {
                wsprintf(szText, "__yes Thread32First pid: 0x%X - tid: 0x%X\n", pid, pte32->th32ThreadID);
                OutputDebugString(szText);
                return pte32->th32ThreadID;
            }
        }
        while (Thread32Next(hSnapThread, pte32));
    }
    else
        FormatErrorMessage("GetLastError ->Thread32First\n", GetLastError());

    CloseHandle(hSnapThread);
    bStarted = 0;
    FREE(pte32);

    OutputDebugString("__finished EnumerateThreads\n");

    return 0;
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
	descriptors_info(aProcesses[i]);
        PrintMemoryInfo(aProcesses[i]);
        EnumerateThreads(aProcesses[i]);
    }

    double startTime[65535];
    for ( i = 0; i < cProcesses; i++ )
    {
	startTime[i] = getCPUTime(aProcesses[i]);
    }
    unsigned int n = i;
    Sleep(1000);
    for ( i = 0; i < n; i++ )
    {
	get_process_name(aProcesses[i]);
	double endTime = getCPUTime(aProcesses[i]);
	fprintf( stderr, "CPU time used = %lf\n", (endTime - startTime[i]) );

    }

    return;
}

void get_tcp_counters(int family)
{
    PMIB_TCPSTATS pTCPStats;
    DWORD dwRetVal = 0;

    pTCPStats = (MIB_TCPSTATS*) MALLOC (sizeof(MIB_TCPSTATS));
    if (pTCPStats == NULL) {
        printf("Error allocating memory\n");
        return 1;
    }

    if ((dwRetVal = GetTcpStatisticsEx(pTCPStats, family)) == NO_ERROR) {
      printf("\tActive Opens: %ld\n", pTCPStats->dwActiveOpens);
      printf("\tPassive Opens: %ld\n", pTCPStats->dwPassiveOpens);
      printf("\tSegments Recv: %ld\n", pTCPStats->dwInSegs);
      printf("\tSegments Xmit: %ld\n", pTCPStats->dwOutSegs);
      printf("\tTotal # Conxs: %ld\n", pTCPStats->dwAttemptFails);
      printf("\tAttemp failed: %ld\n", pTCPStats->dwAttemptFails);
      printf("\tcurr estab: %ld\n", pTCPStats->dwCurrEstab);
      printf("\testab reset: %ld\n", pTCPStats->dwEstabResets);
      printf("\tIn err: %ld\n", pTCPStats->dwInErrs);
      printf("\tmax conn: %ld\n", pTCPStats->dwMaxConn);
      printf("\tNum conn: %ld\n", pTCPStats->dwNumConns);
      printf("\tout rst: %ld\n", pTCPStats->dwOutRsts);
      printf("\tretrans seg: %ld\n", pTCPStats->dwRetransSegs);
       printf("\tRtoAlgorithm: %ld\n", pTCPStats->dwRtoAlgorithm);
        printf("\RtoMax: %ld\n", pTCPStats->dwRtoMax);
         printf("\RtoMin: %ld\n", pTCPStats->dwRtoMin);
          printf("\RtoAlgorithm: %ld\n", pTCPStats->RtoAlgorithm);
    }
    else {
      printf("GetTcpStatistics failed with error: %ld\n", dwRetVal);

      LPVOID lpMsgBuf;
      if (FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        FORMAT_MESSAGE_FROM_SYSTEM | 
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dwRetVal,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
        (LPTSTR) &lpMsgBuf,
        0,
        NULL )) {
        printf("\tError: %s", lpMsgBuf);
      }
      LocalFree( lpMsgBuf );
    }

    if (pTCPStats)
        FREE (pTCPStats);
}

void get_udp_counters(int family)
{
    PMIB_UDPSTATS pUDPStats;
    DWORD dwRetVal = 0;

    pUDPStats = (MIB_UDPSTATS*) MALLOC (sizeof(MIB_UDPSTATS));
    if (pUDPStats == NULL) {
        printf("Error allocating memory\n");
        return 1;
    }

    if ((dwRetVal = GetUdpStatisticsEx(pUDPStats, family)) == NO_ERROR) {
      printf("\tReceive datagrams: %ld\n", pUDPStats->dwInDatagrams);
      printf("\tUDP port errors: %ld\n", pUDPStats->dwNoPorts);
      printf("\tUDP errors: %ld\n", pUDPStats->dwInErrors);
      printf("\tTransmit UDP %ld\n", pUDPStats->dwOutDatagrams);
      printf("\tUDP listens: %ld\n", pUDPStats->dwNumAddrs);
    }
    else {
      printf("GetUdpStatistics failed with error: %ld\n", dwRetVal);

      LPVOID lpMsgBuf;
      if (FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        FORMAT_MESSAGE_FROM_SYSTEM | 
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dwRetVal,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
        (LPTSTR) &lpMsgBuf,
        0,
        NULL )) {
        printf("\tError: %s", lpMsgBuf);
      }
      LocalFree( lpMsgBuf );
    }

    if (pUDPStats)
        FREE (pUDPStats);
}

void get_icmp_counters(int family)
{

    DWORD dwRetVal = 0;
    PMIB_ICMP_EX pIcmpStats;

    pIcmpStats = (MIB_ICMP *) MALLOC(sizeof (MIB_ICMP));
    if (pIcmpStats == NULL) {
        wprintf(L"Error allocating memory\n");
        return;
    }

    dwRetVal = GetIcmpStatisticsEx(pIcmpStats, family);
    if (dwRetVal == NO_ERROR) {
        wprintf(L"Number of incoming ICMP messages: %ld\n",
                pIcmpStats->icmpInStats.dwMsgs);
        wprintf(L"Number of incoming ICMP errors received: %ld\n",
                pIcmpStats->icmpInStats.dwErrors);
        wprintf(L"Number of outgoing ICMP messages: %ld\n",
                pIcmpStats->icmpOutStats.dwMsgs);
        wprintf(L"Number of outgoing ICMP errors sent: %ld\n",
                pIcmpStats->icmpOutStats.dwErrors);
    } else {
        wprintf(L"GetIcmpStatistics failed with error: %ld\n", dwRetVal);
    }

    if (pIcmpStats)
        FREE(pIcmpStats);
}

void get_network_stats()
{
    PMIB_TCPTABLE2 pTcpTable;
    ULONG ulSize = 0;
    DWORD dwRetVal = 0;

    char szLocalAddr[128];
    char szRemoteAddr[128];

    struct in_addr IpAddr;

    int i;

    pTcpTable = (MIB_TCPTABLE2 *) MALLOC(sizeof (MIB_TCPTABLE2));
    if (pTcpTable == NULL) {
        printf("Error allocating memory\n");
        return 1;
    }

    ulSize = sizeof (MIB_TCPTABLE);
// Make an initial call to GetTcpTable2 to
// get the necessary size into the ulSize variable
    if ((dwRetVal = GetTcpTable2(pTcpTable, &ulSize, TRUE)) ==
        ERROR_INSUFFICIENT_BUFFER) {
        FREE(pTcpTable);
        pTcpTable = (MIB_TCPTABLE2 *) MALLOC(ulSize);
        if (pTcpTable == NULL) {
            printf("Error allocating memory\n");
            return 1;
        }
    }
// Make a second call to GetTcpTable2 to get
// the actual data we require
    if ((dwRetVal = GetTcpTable2(pTcpTable, &ulSize, TRUE)) == NO_ERROR) {
        printf("\tNumber of entries: %d\n", (int) pTcpTable->dwNumEntries);
        for (i = 0; i < (int) pTcpTable->dwNumEntries; i++) {
            printf("\n\tTCP[%d] State: %ld - ", i,
                   pTcpTable->table[i].dwState);
            switch (pTcpTable->table[i].dwState) {
            case MIB_TCP_STATE_CLOSED:
                printf("CLOSED\n");
                break;
            case MIB_TCP_STATE_LISTEN:
                printf("LISTEN\n");
                break;
            case MIB_TCP_STATE_SYN_SENT:
                printf("SYN-SENT\n");
                break;
            case MIB_TCP_STATE_SYN_RCVD:
                printf("SYN-RECEIVED\n");
                break;
            case MIB_TCP_STATE_ESTAB:
                printf("ESTABLISHED\n");
                break;
            case MIB_TCP_STATE_FIN_WAIT1:
                printf("FIN-WAIT-1\n");
                break;
            case MIB_TCP_STATE_FIN_WAIT2:
                printf("FIN-WAIT-2 \n");
                break;
            case MIB_TCP_STATE_CLOSE_WAIT:
                printf("CLOSE-WAIT\n");
                break;
            case MIB_TCP_STATE_CLOSING:
                printf("CLOSING\n");
                break;
            case MIB_TCP_STATE_LAST_ACK:
                printf("LAST-ACK\n");
                break;
            case MIB_TCP_STATE_TIME_WAIT:
                printf("TIME-WAIT\n");
                break;
            case MIB_TCP_STATE_DELETE_TCB:
                printf("DELETE-TCB\n");
                break;
            default:
                printf("UNKNOWN dwState value\n");
                break;
            }
            IpAddr.S_un.S_addr = (u_long) pTcpTable->table[i].dwLocalAddr;
            strcpy_s(szLocalAddr, sizeof (szLocalAddr), inet_ntoa(IpAddr));
            printf("\tTCP[%d] Local Addr: %s\n", i, szLocalAddr);
            printf("\tTCP[%d] Local Port: %d \n", i,
                   ntohs((u_short)pTcpTable->table[i].dwLocalPort));

            IpAddr.S_un.S_addr = (u_long) pTcpTable->table[i].dwRemoteAddr;
            strcpy_s(szRemoteAddr, sizeof (szRemoteAddr), inet_ntoa(IpAddr));
            printf("\tTCP[%d] Remote Addr: %s\n", i, szRemoteAddr);
            printf("\tTCP[%d] Remote Port: %d\n", i,
                   ntohs((u_short)pTcpTable->table[i].dwRemotePort));

            printf("\tTCP[%d] Owning PID: %d\n", i, pTcpTable->table[i].dwOwningPid);
            printf("\tTCP[%d] Offload State: %ld - ", i,
                   pTcpTable->table[i].dwOffloadState);
            switch (pTcpTable->table[i].dwOffloadState) {
            case TcpConnectionOffloadStateInHost:
                printf("Owned by the network stack and not offloaded \n");
                break;
            case TcpConnectionOffloadStateOffloading:
                printf("In the process of being offloaded\n");
                break;
            case TcpConnectionOffloadStateOffloaded:
                printf("Offloaded to the network interface control\n");
                break;
            case TcpConnectionOffloadStateUploading:
                printf("In the process of being uploaded back to the network stack \n");
                break;
            default:
                printf("UNKNOWN Offload state value\n");
                break;
            }

        }
    } else {
        printf("\tGetTcpTable2 failed with %d\n", dwRetVal);
        FREE(pTcpTable);
        return 1;
    }

    if (pTcpTable != NULL) {
        FREE(pTcpTable);
        pTcpTable = NULL;
    }
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
	get_network_stats();
	get_tcp_counters(AF_INET);
	get_tcp_counters(AF_INET6);
	get_udp_counters(AF_INET);
	get_udp_counters(AF_INET6);
	//get_icmp_counters(AF_INET);
	//get_icmp_counters(AF_INET6);
}
#endif
