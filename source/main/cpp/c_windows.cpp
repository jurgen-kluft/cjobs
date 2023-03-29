#include "cjobs/c_jobs.h"
#include "cjobs/private/c_timer.h"

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

namespace cjobs
{
    void g_Printf(const char* format, ...)
    {
        va_list argptr;
        va_start(argptr, fmt);
        // common->VPrintf( fmt, argptr );
        va_end(argptr);
    }

    void g_Error(const char* format, ...);
    bool g_AssertFailed(const char* file, int line, const char* expression);

    uint64 Timer::mFrequency = 0;

    void Timer::Init()
    {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        mFrequency = static_cast<double>(frequency.QuadPart);
    }

    ticks_t Timer::Current()
    {
        LARGE_INTEGER ticks;
        QueryPerformanceCounter(&ticks);
        return static_cast<ticks_t>(ticks.QuadPart);
    }

    ticks_t Timer::Lap(ticks_t time)
    {
        LARGE_INTEGER ticks;
        QueryPerformanceCounter(&ticks);
        ticks_t current = static_cast<ticks_t>(ticks.QuadPart);
        return current - time;
    }

    double Timer::ElapsedMs(ticks_t time)
    {
        LARGE_INTEGER ticks;
        QueryPerformanceCounter(&ticks);
        return (static_cast<double>(ticks.QuadPart - time) * 1000.0) / mFrequency;
    }

    uint64 g_Microseconds()
    {
        double ms = cjobs::Timer::ElapsedMs(0);
        return static_cast<uint64>(ms * 1000.0);
    }


} // namespace cjobs