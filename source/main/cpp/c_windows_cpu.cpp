
#if defined(_MSC_VER) && !defined(__clang__) && !defined(__GNUC__) && !defined(NOMINMAX)
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#else
#include <windows.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// LogicalProcPerPhysicalProc
#define NUM_LOGICAL_BITS \
    0x00FF0000 // EBX[23:16] Bit 16-23 in ebx contains the number of logical
               // processors per physical processor when execute cpuid with
               // eax set to 1
static unsigned char LogicalProcPerPhysicalProc()
{
    unsigned int regebx = 0;
    __asm {
		mov eax, 1
		cpuid
		mov regebx, ebx
    }
    return (unsigned char)((regebx & NUM_LOGICAL_BITS) >> 16);
}

// GetAPIC_ID
#define INITIAL_APIC_ID_BITS \
    0xFF000000 // EBX[31:24] Bits 24-31 (8 bits) return the 8-bit unique
               // initial APIC ID for the processor this code is running on.
               // Default value = 0xff if HT is not supported
static unsigned char GetAPIC_ID()
{
    unsigned int regebx = 0;
    __asm {
		mov eax, 1
		cpuid
		mov regebx, ebx
    }
    return (unsigned char)((regebx & INITIAL_APIC_ID_BITS) >> 24);
}

// CPUCount
#define HT_NOT_CAPABLE 0
#define HT_ENABLED 1
#define HT_DISABLED 2
#define HT_SUPPORTED_NOT_ENABLED 3
#define HT_CANNOT_DETECT 4

int CPUCount(int& logicalNum, int& physicalNum)
{
    int         statusFlag;
    SYSTEM_INFO info;

    physicalNum = 1; // the total number of physical processor
    logicalNum  = 1; // the number of logical CPU per physical CPU

    statusFlag  = HT_NOT_CAPABLE;

    info.dwNumberOfProcessors = 0;
    GetSystemInfo(&info);

    // Number of physical processors in a non-Intel system
    // or in a 32-bit Intel system with Hyper-Threading technology disabled
    physicalNum = info.dwNumberOfProcessors;

    unsigned char HT_Enabled = 0;

    logicalNum = LogicalProcPerPhysicalProc();

    if (logicalNum >= 1)
    { // > 1 doesn't mean HT is enabled in the BIOS
        HANDLE hCurrentProcessHandle;
        DWORD  dwProcessAffinity;
        DWORD  dwSystemAffinity;
        DWORD  dwAffinityMask;

        // Calculate the appropriate  shifts and mask based on the
        // number of logical processors.

        unsigned char i = 1, PHY_ID_MASK = 0xFF, PHY_ID_SHIFT = 0;

        while (i < logicalNum)
        {
            i *= 2;
            PHY_ID_MASK <<= 1;
            PHY_ID_SHIFT++;
        }

        hCurrentProcessHandle = GetCurrentProcess();
        GetProcessAffinityMask(hCurrentProcessHandle, &dwProcessAffinity, &dwSystemAffinity);

        // Check if available process affinity mask is equal to the
        // available system affinity mask
        if (dwProcessAffinity != dwSystemAffinity)
        {
            statusFlag  = HT_CANNOT_DETECT;
            physicalNum = -1;
            return statusFlag;
        }

        dwAffinityMask = 1;
        while (dwAffinityMask != 0 && dwAffinityMask <= dwProcessAffinity)
        {
            // Check if this CPU is available
            if (dwAffinityMask & dwProcessAffinity)
            {
                if (SetProcessAffinityMask(hCurrentProcessHandle, dwAffinityMask))
                {
                    unsigned char APIC_ID, LOG_ID, PHY_ID;

                    Sleep(0); // Give OS time to switch CPU

                    APIC_ID = GetAPIC_ID();
                    LOG_ID  = APIC_ID & ~PHY_ID_MASK;
                    PHY_ID  = APIC_ID >> PHY_ID_SHIFT;

                    if (LOG_ID != 0)
                    {
                        HT_Enabled = 1;
                    }
                }
            }
            dwAffinityMask = dwAffinityMask << 1;
        }

        // Reset the processor affinity
        SetProcessAffinityMask(hCurrentProcessHandle, dwProcessAffinity);

        if (logicalNum == 1)
        { // Normal P4 : HT is disabled in hardware
            statusFlag = HT_DISABLED;
        }
        else
        {
            if (HT_Enabled)
            {
                // Total physical processors in a Hyper-Threading enabled system.
                physicalNum /= logicalNum;
                statusFlag = HT_ENABLED;
            }
            else
            {
                statusFlag = HT_SUPPORTED_NOT_ENABLED;
            }
        }
    }
    return statusFlag;
}

typedef BOOL(WINAPI* LPFN_GLPI)(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION, PDWORD);

enum LOGICAL_PROCESSOR_RELATIONSHIP_LOCAL
{
    localRelationProcessorCore,
    localRelationNumaNode,
    localRelationCache,
    localRelationProcessorPackage
};

struct cpuInfo_t
{
    int processorPackageCount;
    int processorCoreCount;
    int logicalProcessorCount;
    int numaNodeCount;
    struct cacheInfo_t
    {
        int count;
        int associativity;
        int lineSize;
        int size;
    } cacheLevel[3];
};

DWORD CountSetBits(ULONG_PTR bitMask)
{
    DWORD     LSHIFT      = sizeof(ULONG_PTR) * 8 - 1;
    DWORD     bitSetCount = 0;
    ULONG_PTR bitTest     = (ULONG_PTR)1 << LSHIFT;

    for (DWORD i = 0; i <= LSHIFT; i++)
    {
        bitSetCount += ((bitMask & bitTest) ? 1 : 0);
        bitTest /= 2;
    }

    return bitSetCount;
}

/*
========================
GetCPUInfo
========================
*/
bool GetCPUInfo(cpuInfo_t& cpuInfo)
{
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = NULL;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION ptr    = NULL;
    PCACHE_DESCRIPTOR                     Cache;
    LPFN_GLPI                             glpi;
    BOOL                                  done         = FALSE;
    DWORD                                 returnLength = 0;
    DWORD                                 byteOffset   = 0;

    memset(&cpuInfo, 0, sizeof(cpuInfo));

    glpi = (LPFN_GLPI)GetProcAddress(GetModuleHandle(TEXT("kernel32")), "GetLogicalProcessorInformation");
    if (NULL == glpi)
    {
        g_Printf("\nGetLogicalProcessorInformation is not supported.\n");
        return 0;
    }

    while (!done)
    {
        DWORD rc = glpi(buffer, &returnLength);

        if (FALSE == rc)
        {
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
            {
                if (buffer)
                {
                    free(buffer);
                }

                buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)malloc(returnLength);
            }
            else
            {
                g_Printf("Sys_CPUCount error: %d\n", GetLastError());
                return false;
            }
        }
        else
        {
            done = TRUE;
        }
    }

    ptr = buffer;

    while (byteOffset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) <= returnLength)
    {
        switch ((LOGICAL_PROCESSOR_RELATIONSHIP_LOCAL)ptr->Relationship)
        {
            case localRelationProcessorCore:
                cpuInfo.processorCoreCount++;

                // A hyperthreaded core supplies more than one logical processor.
                cpuInfo.logicalProcessorCount += CountSetBits(ptr->ProcessorMask);
                break;

            case localRelationNumaNode:
                // Non-NUMA systems report a single record of this type.
                cpuInfo.numaNodeCount++;
                break;

            case localRelationCache:
                // Cache data is in ptr->Cache, one CACHE_DESCRIPTOR structure for each cache.
                Cache = &ptr->Cache;
                if (Cache->Level >= 1 && Cache->Level <= 3)
                {
                    int level = Cache->Level - 1;
                    if (cpuInfo.cacheLevel[level].count > 0)
                    {
                        cpuInfo.cacheLevel[level].count++;
                    }
                    else
                    {
                        cpuInfo.cacheLevel[level].associativity = Cache->Associativity;
                        cpuInfo.cacheLevel[level].lineSize      = Cache->LineSize;
                        cpuInfo.cacheLevel[level].size          = Cache->Size;
                    }
                }
                break;

            case localRelationProcessorPackage:
                // Logical processors share a physical package.
                cpuInfo.processorPackageCount++;
                break;

            default: g_Printf("Error: Unsupported LOGICAL_PROCESSOR_RELATIONSHIP value.\n"); break;
        }
        byteOffset += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
        ptr++;
    }

    free(buffer);

    return true;
}

namespace cjobs
{
    typedef int int32;

    void g_CPUCount(int32& logicalNum, int32& coreNum, int32& packageNum)
    {
        cpuInfo_t cpuInfo;
        GetCPUInfo(cpuInfo);

        coreNum    = cpuInfo.processorCoreCount;
        logicalNum = cpuInfo.logicalProcessorCount;
        packageNum = cpuInfo.processorPackageCount;
    }
} // namespace cjobs