#include "cjobs/c_jobs.h"
#include "cjobs/private/c_timer.h"
#include "cjobs/private/c_threading.h"

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

#include <thread>

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

    namespace cthread
    {
        void SetThreadName(DWORD threadID, const char* name)
        {
            THREADNAME_INFO info;
            info.dwType     = 0x1000;
            info.szName     = name;
            info.dwThreadID = threadID;
            info.dwFlags    = 0;
        }
        
        uintptr_t CreateThread(ThreadFunc_t function, void* parms, EPriority priority, const char* name, core_t core, int stackSize, bool suspended)
        {

            DWORD flags = (suspended ? CREATE_SUSPENDED : 0);
            // Without this flag the 'dwStackSize' parameter to CreateThread specifies the "Stack Commit Size"
            // and the "Stack Reserve Size" is set to the value specified at link-time.
            // With this flag the 'dwStackSize' parameter to CreateThread specifies the "Stack Reserve Size"
            // and the “Stack Commit Size” is set to the value specified at link-time.
            // For various reasons (some of which historic) we reserve a large amount of stack space in the
            // project settings. By setting this flag and by specifying 64 kB for the "Stack Commit Size" in
            // the project settings we can create new threads with a much smaller reserved (and committed)
            // stack space. It is very important that the "Stack Commit Size" is set to a small value in
            // the project settings. If it is set to a large value we may be both reserving and committing
            // a lot of memory by setting the STACK_SIZE_PARAM_IS_A_RESERVATION flag. There are some
            // 50 threads allocated for normal game play. If, for instance, the commit size is set to 16 MB
            // then by adding this flag we would be reserving and committing 50 x 16 = 800 MB of memory.
            // On the other hand, if this flag is not set and the "Stack Reserve Size" is set to 16 MB in the
            // project settings, then we would still be reserving 50 x 16 = 800 MB of virtual address space.
            flags |= STACK_SIZE_PARAM_IS_A_RESERVATION;

            DWORD  threadId;
            HANDLE handle = CreateThread(NULL, // LPSECURITY_ATTRIBUTES lpsa, //-V513
                                         stackSize, (LPTHREAD_START_ROUTINE)function, parms, flags, &threadId);
            if (handle == 0)
            {
                idLib::common->FatalError("CreateThread error: %i", GetLastError());
                return (uintptr_t)0;
            }
            SetThreadName(threadId, name);
            if (priority == PRIORITY_HIGHEST)
            {
                SetThreadPriority((HANDLE)handle, THREAD_PRIORITY_HIGHEST); //  we better sleep enough to do this
            }
            else if (priority == PRIORITY_ABOVE_NORMAL)
            {
                SetThreadPriority((HANDLE)handle, THREAD_PRIORITY_ABOVE_NORMAL);
            }
            else if (priority == PRIORITY_BELOW_NORMAL)
            {
                SetThreadPriority((HANDLE)handle, THREAD_PRIORITY_BELOW_NORMAL);
            }
            else if (priority == PRIORITY_LOWEST)
            {
                SetThreadPriority((HANDLE)handle, THREAD_PRIORITY_LOWEST);
            }

            // Under Windows, we don't set the thread affinity and let the OS deal with scheduling

            return (uintptr_t)handle;
        }

        uintptr_t GetCurrentThreadID() { return GetCurrentThreadId(); }
        void      WaitForThread(uintptr_t threadHandle) { WaitForSingleObject((HANDLE)threadHandle, INFINITE); }

        void DestroyThread(uintptr_t threadHandle)
        {
            if (threadHandle == 0)
            {
                return;
            }
            WaitForSingleObject((HANDLE)threadHandle, INFINITE);
            CloseHandle((HANDLE)threadHandle);
        }

        void Yield() { SwitchToThread(); }

        void SignalCreate(signalHandle_t& handle, bool manualReset) { handle = CreateEvent(NULL, manualReset, FALSE, NULL); }
        void SignalDestroy(signalHandle_t& handle) { CloseHandle(handle); }
        void SignalRaise(signalHandle_t& handle) { SetEvent(handle); }
        void SignalClear(signalHandle_t& handle)
        {
            // events are created as auto-reset so this should never be needed
            ResetEvent(handle);
        }

        bool SignalWait(signalHandle_t& handle, int timeout)
        {
            DWORD result = WaitForSingleObject(handle, timeout == idSysSignal::WAIT_INFINITE ? INFINITE : timeout);
            assert(result == WAIT_OBJECT_0 || (timeout != idSysSignal::WAIT_INFINITE && result == WAIT_TIMEOUT));
            return (result == WAIT_OBJECT_0);
        }

        void MutexCreate(mutexHandle_t& handle) { InitializeCriticalSection(&handle); }
        void MutexDestroy(mutexHandle_t& handle) { DeleteCriticalSection(&handle); }

        bool Sys_MutexLock(mutexHandle_t& handle, bool blocking)
        {
            if (TryEnterCriticalSection(&handle) == 0)
            {
                if (!blocking)
                {
                    return false;
                }
                EnterCriticalSection(&handle);
            }
            return true;
        }

        void MutexUnlock(mutexHandle_t& handle) { LeaveCriticalSection(&handle); }

        interlockedInt_t InterlockedIncrement(interlockedInt_t& value) { return InterlockedIncrementAcquire(&value); }
        interlockedInt_t InterlockedDecrement(interlockedInt_t& value) { return InterlockedDecrementRelease(&value); }
        interlockedInt_t InterlockedAdd(interlockedInt_t& value, interlockedInt_t i) { return InterlockedExchangeAdd(&value, i) + i; }
        interlockedInt_t InterlockedSub(interlockedInt_t& value, interlockedInt_t i) { return InterlockedExchangeAdd(&value, -i) - i; }
        interlockedInt_t InterlockedExchange(interlockedInt_t& value, interlockedInt_t exchange) { return InterlockedExchange(&value, exchange); }
        interlockedInt_t InterlockedCompareExchange(interlockedInt_t& value, interlockedInt_t comparand, interlockedInt_t exchange) { return InterlockedCompareExchange(&value, exchange, comparand); }

        void* InterlockedExchangePointer(void*& ptr, void* exchange) { return InterlockedExchangePointer(&ptr, exchange); }
        void* InterlockedCompareExchangePointer(void*& ptr, void* comparand, void* exchange) { return InterlockedCompareExchangePointer(&ptr, exchange, comparand); }
    } // namespace cthread

} // namespace cjobs