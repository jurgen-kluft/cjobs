#include "cjobs/private/c_hwinfo.h"

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

#include <WbemIdl.h>
#include <comdef.h>
#include <ntddscsi.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "wbemuuid.lib")

namespace hwinfo
{
    // https://github.com/lfreist/hwinfo/blob/main/include/hwinfo/WMIwrapper.h
    namespace wmi
    {
        template <typename T> inline bool queryWMI(const std::string& WMIClass, std::string field, std::vector<T>& value, const std::string& serverName = "ROOT\\CIMV2")
        {
            std::string query("SELECT " + field + " FROM " + WMIClass);

            HRESULT hres;
            hres = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (FAILED(hres))
            {
                return false;
            }
            hres = CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
            if (FAILED(hres))
            {
                CoUninitialize();
                return false;
            }
            IWbemLocator* pLoc = nullptr;
            hres               = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc);
            if (FAILED(hres))
            {
                CoUninitialize();
                return false;
            }
            IWbemServices* pSvc = nullptr;
            hres                = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, nullptr, 0, nullptr, nullptr, &pSvc);
            if (FAILED(hres))
            {
                pLoc->Release();
                CoUninitialize();
                return false;
            }
            hres = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
            if (FAILED(hres))
            {
                pSvc->Release();
                pLoc->Release();
                CoUninitialize();
                return false;
            }
            IEnumWbemClassObject* pEnumerator = nullptr;
            hres                              = pSvc->ExecQuery(bstr_t(L"WQL"), bstr_t(std::wstring(query.begin(), query.end()).c_str()), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &pEnumerator);
            if (FAILED(hres))
            {
                pSvc->Release();
                pLoc->Release();
                CoUninitialize();
                return false;
            }
            IWbemClassObject* pclsObj = nullptr;
            ULONG             uReturn = 0;
            while (pEnumerator)
            {
                pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);

                if (!uReturn)
                {
                    break;
                }

                VARIANT vtProp;
                pclsObj->Get(std::wstring(field.begin(), field.end()).c_str(), 0, &vtProp, nullptr, nullptr);

                if (std::is_same<T, long>::value || std::is_same<T, int>::value)
                {
                    value.push_back((T)vtProp.intVal);
                }
                else if (std::is_same<T, bool>::value)
                {
                    value.push_back((T)vtProp.boolVal);
                }
                else if (std::is_same<T, unsigned>::value)
                {
                    value.push_back((T)vtProp.uintVal);
                }
                else if (std::is_same<T, unsigned short>::value)
                {
                    value.push_back((T)vtProp.uiVal);
                }
                else if (std::is_same<T, long long>::value)
                {
                    value.push_back((T)vtProp.llVal);
                }
                else if (std::is_same<T, unsigned long long>::value)
                {
                    value.push_back((T)vtProp.ullVal);
                }
                else
                {
                    value.push_back((T)((bstr_t)vtProp.bstrVal).copy());
                }

                VariantClear(&vtProp);
                pclsObj->Release();
            }

            if (value.empty())
            {
                value.resize(1);
            }

            pSvc->Release();
            pLoc->Release();
            if (pEnumerator)
                pEnumerator->Release();
            CoUninitialize();
            return true;
        }
    } // namespace wmi

    void getCPUInfo(int32& numLogicalCores, int32& numPhysicalCores, int32& numPackages)
    {
        static bool    initialized = false;
        static int32_t logicalCores;
        static int32_t physicalCores;
        static int32_t packages;

        // Only initialize once
        if (!initialized)
        {
            wmi::queryWMI("Win32_Processor", "NumberOfLogicalProcessors", logicalCores);
            wmi::queryWMI("Win32_Processor", "NumberOfCores", physicalCores);
            wmi::queryWMI("Win32_ComputerSystem", "NumberOfProcessors", packages);
            initialized = true;
        }
        numLogicalCores  = logicalCores;
        numPhysicalCores = physicalCores;
        numPackages      = packages;
    }

    // _____________________________________________________________________________________________________________________
    int CPU::currentClockSpeed_kHz()
    {
        std::vector<int64_t> speed{};
        wmi::queryWMI("Win32_Processor", "CurrentClockSpeed", speed);
        if (speed.empty())
        {
            return -1;
        }
        return speed[0];
    }

    // _____________________________________________________________________________________________________________________
    std::string CPU::getVendor()
    {
        std::vector<const wchar_t*> vendor{};
        wmi::queryWMI("Win32_Processor", "Manufacturer", vendor);
        if (vendor.empty())
        {
#if defined(HWINFO_X86)
            std::string v;
            uint32_t    regs[4]{0};
            cpuid::cpuid(0, 0, regs);
            v += std::string((const char*)&regs[1], 4);
            v += std::string((const char*)&regs[3], 4);
            v += std::string((const char*)&regs[2], 4);
            return v;
#else
            return "<unknown>";
#endif
        }
        std::wstring tmp(vendor[0]);
        return {tmp.begin(), tmp.end()};
        return "<unknown>";
    }

    // _____________________________________________________________________________________________________________________
    std::string CPU::getModelName()
    {
        std::vector<const wchar_t*> vendor{};
        wmi::queryWMI("Win32_Processor", "Name", vendor);
        if (vendor.empty())
        {
#if defined(HWINFO_X86)
            std::string model;
            uint32_t    regs[4]{};
            for (unsigned i = 0x80000002; i < 0x80000005; ++i)
            {
                cpuid::cpuid(i, 0, regs);
                for (auto c : std::string((const char*)&regs[0], 4))
                {
                    if (std::isalnum(c) || c == '(' || c == ')' || c == '@' || c == ' ' || c == '-' || c == '.')
                    {
                        model += c;
                    }
                }
                for (auto c : std::string((const char*)&regs[1], 4))
                {
                    if (std::isalnum(c) || c == '(' || c == ')' || c == '@' || c == ' ' || c == '-' || c == '.')
                    {
                        model += c;
                    }
                }
                for (auto c : std::string((const char*)&regs[2], 4))
                {
                    if (std::isalnum(c) || c == '(' || c == ')' || c == '@' || c == ' ' || c == '-' || c == '.')
                    {
                        model += c;
                    }
                }
                for (auto c : std::string((const char*)&regs[3], 4))
                {
                    if (std::isalnum(c) || c == '(' || c == ')' || c == '@' || c == ' ' || c == '-' || c == '.')
                    {
                        model += c;
                    }
                }
            }
            return model;
#else
            return "<unknown>";
#endif
        }
        std::wstring tmp(vendor[0]);
        return {tmp.begin(), tmp.end()};
    }

    // _____________________________________________________________________________________________________________________
    int CPU::getNumPhysicalCores()
    {
        std::vector<int> cores{};
        wmi::queryWMI("Win32_Processor", "NumberOfCores", cores);
        if (cores.empty())
        {
#if defined(HWINFO_X86)
            uint32_t    regs[4]{};
            std::string vendorId = getVendor();
            std::for_each(vendorId.begin(), vendorId.end(), [](char& in) { in = ::toupper(in); });
            cpuid::cpuid(0, 0, regs);
            uint32_t HFS = regs[0];
            if (vendorId.find("INTEL") != std::string::npos)
            {
                if (HFS >= 11)
                {
                    for (int lvl = 0; lvl < MAX_INTEL_TOP_LVL; ++lvl)
                    {
                        uint32_t regs_2[4]{};
                        cpuid::cpuid(0x0b, lvl, regs_2);
                        uint32_t currLevel = (LVL_TYPE & regs_2[2]) >> 8;
                        if (currLevel == 0x01)
                        {
                            int numCores = getNumLogicalCores() / static_cast<int>(LVL_CORES & regs_2[1]);
                            if (numCores > 0)
                            {
                                return numCores;
                            }
                        }
                    }
                }
                else
                {
                    if (HFS >= 4)
                    {
                        uint32_t regs_3[4]{};
                        cpuid::cpuid(4, 0, regs_3);
                        int numCores = getNumLogicalCores() / static_cast<int>(1 + ((regs_3[0] >> 26) & 0x3f));
                        if (numCores > 0)
                        {
                            return numCores;
                        }
                    }
                }
            }
            else if (vendorId.find("AMD") != std::string::npos)
            {
                if (HFS > 0)
                {
                    uint32_t regs_4[4]{};
                    cpuid::cpuid(0x80000000, 0, regs_4);
                    if (regs_4[0] >= 8)
                    {
                        int numCores = 1 + (regs_4[2] & 0xff);
                        return numCores;
                    }
                }
            }
            return -1;
#else
            return -1;
#endif
        }
        return cores[0];
    }

    // _____________________________________________________________________________________________________________________
    int CPU::getNumLogicalCores()
    {
        std::vector<int> cores{};
        wmi::queryWMI("Win32_Processor", "NumberOfLogicalProcessors", cores);
        if (cores.empty())
        {
#if defined(HWINFO_X86)
            std::string vendorId = getVendor();
            std::for_each(vendorId.begin(), vendorId.end(), [](char& in) { in = ::toupper(in); });
            uint32_t regs[4]{};
            cpuid::cpuid(0, 0, regs);
            uint32_t HFS = regs[0];
            if (vendorId.find("INTEL") != std::string::npos)
            {
                if (HFS >= 0xb)
                {
                    for (int lvl = 0; lvl < MAX_INTEL_TOP_LVL; ++lvl)
                    {
                        uint32_t regs_2[4]{};
                        cpuid::cpuid(0x0b, lvl, regs_2);
                        uint32_t currLevel = (LVL_TYPE & regs_2[2]) >> 8;
                        if (currLevel == 0x02)
                        {
                            return static_cast<int>(LVL_CORES & regs_2[1]);
                        }
                    }
                }
            }
            else if (vendorId.find("AMD") != std::string::npos)
            {
                if (HFS > 0)
                {
                    cpuid::cpuid(1, 0, regs);
                    return static_cast<int>(regs[1] >> 16) & 0xff;
                }
                return 1;
            }
            return -1;
#else
            return -1;
#endif
        }
        return cores[0];
    }

    // _____________________________________________________________________________________________________________________
    int CPU::getMaxClockSpeed_kHz()
    {
        std::vector<int64_t> speed{};
        wmi::queryWMI("Win32_Processor", "MaxClockSpeed", speed);
        if (speed.empty())
        {
            return -1;
        }
        return speed[0] * 1000;
    }

    // _____________________________________________________________________________________________________________________
    int CPU::getRegularClockSpeed_kHz()
    {
        std::vector<int64_t> speed{};
        wmi::queryWMI("Win32_Processor", "CurrentClockSpeed", speed);
        if (speed.empty())
        {
            return -1;
        }
        return speed[0] * 1000;
    }

    int CPU::getCacheSize_Bytes()
    {
        std::vector<int64_t> cacheSize{};
        wmi::queryWMI("Win32_Processor", "L3CacheSize", cacheSize);
        if (cacheSize.empty())
        {
            return -1;
        }
        return cacheSize[0] * 1024;
    }

} // namespace hwinfo