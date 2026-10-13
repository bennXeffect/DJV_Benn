// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/App/SystemMemory.h>

#include <ftk/Core/Format.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif // NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <dxgi1_4.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#else
#include <fstream>
#include <unistd.h>
#include <sys/sysinfo.h>
#endif

namespace djv
{
    namespace app
    {
        namespace
        {
#if defined(_WIN32)
            // The adapter with the most memory of its own, which is the one
            // a laptop with two drives the window with when it matters.
            // Found once: enumerating adapters every second costs more than
            // asking one.
            IDXGIAdapter3* getAdapter()
            {
                static bool init = false;
                static IDXGIAdapter3* adapter = nullptr;
                if (!init)
                {
                    init = true;
                    IDXGIFactory1* factory = nullptr;
                    if (SUCCEEDED(CreateDXGIFactory1(
                        __uuidof(IDXGIFactory1),
                        reinterpret_cast<void**>(&factory))))
                    {
                        SIZE_T best = 0;
                        IDXGIAdapter1* a = nullptr;
                        for (UINT i = 0; factory->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i)
                        {
                            DXGI_ADAPTER_DESC1 desc;
                            if (SUCCEEDED(a->GetDesc1(&desc)) &&
                                !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                                desc.DedicatedVideoMemory > best)
                            {
                                IDXGIAdapter3* a3 = nullptr;
                                if (SUCCEEDED(a->QueryInterface(
                                    __uuidof(IDXGIAdapter3),
                                    reinterpret_cast<void**>(&a3))))
                                {
                                    if (adapter)
                                    {
                                        adapter->Release();
                                    }
                                    adapter = a3;
                                    best = desc.DedicatedVideoMemory;
                                }
                            }
                            a->Release();
                        }
                        factory->Release();
                    }
                }
                return adapter;
            }
#endif // _WIN32

            double toGB(uint64_t value)
            {
                return value / (1024.0 * 1024.0 * 1024.0);
            }
        }

        SystemMemory getSystemMemory()
        {
            SystemMemory out;
#if defined(_WIN32)
            PROCESS_MEMORY_COUNTERS_EX pmc;
            if (GetProcessMemoryInfo(
                GetCurrentProcess(),
                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                sizeof(pmc)))
            {
                // The private bytes, what the process holds for itself --
                // the image cache lives here -- rather than the working set,
                // which goes up and down with what Windows pages.
                out.processRAM = pmc.PrivateUsage;
            }
            MEMORYSTATUSEX status;
            status.dwLength = sizeof(status);
            if (GlobalMemoryStatusEx(&status))
            {
                out.systemRAMTotal = status.ullTotalPhys;
                out.systemRAMUsed = status.ullTotalPhys - status.ullAvailPhys;
            }
            if (IDXGIAdapter3* adapter = getAdapter())
            {
                DXGI_QUERY_VIDEO_MEMORY_INFO info;
                if (SUCCEEDED(adapter->QueryVideoMemoryInfo(
                    0,
                    DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
                    &info)))
                {
                    DXGI_ADAPTER_DESC1 desc;
                    if (SUCCEEDED(adapter->GetDesc1(&desc)))
                    {
                        out.vramValid = true;
                        out.vramUsed = info.CurrentUsage;
                        out.vramTotal = desc.DedicatedVideoMemory;
                    }
                }
            }
#elif defined(__APPLE__)
            mach_task_basic_info info;
            mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
            if (KERN_SUCCESS == task_info(
                mach_task_self(),
                MACH_TASK_BASIC_INFO,
                reinterpret_cast<task_info_t>(&info),
                &count))
            {
                out.processRAM = info.resident_size;
            }
            int64_t total = 0;
            size_t size = sizeof(total);
            if (0 == sysctlbyname("hw.memsize", &total, &size, nullptr, 0))
            {
                out.systemRAMTotal = static_cast<uint64_t>(total);
            }
#else
            {
                std::ifstream f("/proc/self/statm");
                uint64_t pages = 0;
                uint64_t resident = 0;
                if (f >> pages >> resident)
                {
                    out.processRAM = resident * sysconf(_SC_PAGESIZE);
                }
            }
            struct sysinfo info;
            if (0 == sysinfo(&info))
            {
                out.systemRAMTotal = static_cast<uint64_t>(info.totalram) * info.mem_unit;
                out.systemRAMUsed = static_cast<uint64_t>(info.totalram - info.freeram) * info.mem_unit;
            }
#endif
            return out;
        }

        std::string getSystemMemoryLabel(const SystemMemory& value)
        {
            std::string out = ftk::Format("RAM: {0} GB").arg(toGB(value.processRAM), 1);
            if (value.systemRAMTotal > 0 && value.systemRAMUsed > 0)
            {
                out += ftk::Format(" ({0}/{1} GB)").
                    arg(toGB(value.systemRAMUsed), 1).
                    arg(toGB(value.systemRAMTotal), 1).str();
            }
            if (value.vramValid)
            {
                out += ftk::Format("  VRAM: {0}/{1} GB").
                    arg(toGB(value.vramUsed), 1).
                    arg(toGB(value.vramTotal), 1).str();
            }
            return out;
        }
    }
}
