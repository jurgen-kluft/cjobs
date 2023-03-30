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
        // to be implemented by the user (redirect to existing printf code)
    }

    void g_Error(const char* format, ...)
    {
        // to be implemented by the user (redirect to existing error handling code)
    }

    void g_AssertFailed(const char* file, int line, const char* expression)
    {
        // to be implemented by the user (redirect to existing assert code)
    }

    uint64 Timer::mFrequency = 0;

    void SysTimer::Init()
    {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        mFrequency = static_cast<double>(frequency.QuadPart);
    }

    ticks_t SysTimer::Current()
    {
        LARGE_INTEGER ticks;
        QueryPerformanceCounter(&ticks);
        return static_cast<ticks_t>(ticks.QuadPart);
    }

    ticks_t SysTimer::Lap(ticks_t time)
    {
        LARGE_INTEGER ticks;
        QueryPerformanceCounter(&ticks);
        ticks_t current = static_cast<ticks_t>(ticks.QuadPart);
        return current - time;
    }

    double SysTimer::ElapsedMs(ticks_t time)
    {
        LARGE_INTEGER ticks;
        QueryPerformanceCounter(&ticks);
        return (static_cast<double>(ticks.QuadPart - time) * 1000.0) / mFrequency;
    }

    uint64 g_Microseconds()
    {
        double ms = cjobs::SysTimer::ElapsedMs(0);
        return static_cast<uint64>(ms * 1000.0);
    }

    namespace cthread
    {
        bool SysSetThreadName(threadHandle_t handle, const char* name)
        {
            RESULT hr = SetThreadDescription(handle, name);
            if (FAILED(hr))
            {
                return false;
            }
            return true;
        }

        threadHandle_t SysCreateThread(ThreadFunc_t function, void* parms, EPriority priority, const char* name, core_t core, int stackSize, bool suspended)
        {
            DWORD flags = (suspended ? CREATE_SUSPENDED : 0);
            flags |= STACK_SIZE_PARAM_IS_A_RESERVATION;

            DWORD  threadId;
            HANDLE handle = _beginthreadex(NULL, stackSize, function, parms, flags, &threadId);
            if (handle == 0)
            {
                idLib::common->FatalError("CreateThread error: %i", GetLastError());
                return (threadHandle_t)0;
            }
            SysSetThreadName(threadId, name);
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

            return (threadHandle_t)handle;
        }

        threadId_t SysGetCurrentThreadID() { return (threadId_t)::GetCurrentThreadId(); }
        threadHandle_t SysGetCurrentThread() { return (threadHandle_t)::GetCurrentThread(); }

        void      SysWaitForThread(threadHandle_t threadHandle) { WaitForSingleObject((HANDLE)threadHandle, INFINITE); }

        void SysDestroyThread(threadHandle_t threadHandle)
        {
            if (threadHandle == 0)
            {
                return;
            }
            WaitForSingleObject((HANDLE)threadHandle, INFINITE);
            CloseHandle((HANDLE)threadHandle);
        }

        void SysYield() { SwitchToThread(); }

        void SysSignalCreate(signalHandle_t& handle, bool manualReset) { handle = CreateEvent(NULL, manualReset, FALSE, NULL); }
        void SysSignalDestroy(signalHandle_t& handle) { CloseHandle(handle); }
        void SysSignalRaise(signalHandle_t& handle) { SetEvent(handle); }
        void SysSignalClear(signalHandle_t& handle)
        {
            // events are created as auto-reset so this should never be needed
            ResetEvent(handle);
        }

        bool SysSignalWait(signalHandle_t& handle, int timeout)
        {
            DWORD result = WaitForSingleObject(handle, timeout == SysSignal::WAIT_INFINITE ? INFINITE : timeout);
            assert(result == WAIT_OBJECT_0 || (timeout != SysSignal::WAIT_INFINITE && result == WAIT_TIMEOUT));
            return (result == WAIT_OBJECT_0);
        }

        void SysMutexCreate(mutexHandle_t& handle) { InitializeCriticalSection(&handle); }
        void SysMutexDestroy(mutexHandle_t& handle) { DeleteCriticalSection(&handle); }

        bool SysMutexLock(mutexHandle_t& handle, bool blocking)
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

        void SysMutexUnlock(mutexHandle_t& handle) { LeaveCriticalSection(&handle); }

        interlockedInt_t SysInterlockedIncrement(interlockedInt_t& value) { return InterlockedIncrementAcquire(&value); }
        interlockedInt_t SysInterlockedDecrement(interlockedInt_t& value) { return InterlockedDecrementRelease(&value); }
        interlockedInt_t SysInterlockedAdd(interlockedInt_t& value, interlockedInt_t i) { return InterlockedExchangeAdd(&value, i) + i; }
        interlockedInt_t SysInterlockedSub(interlockedInt_t& value, interlockedInt_t i) { return InterlockedExchangeAdd(&value, -i) - i; }
        interlockedInt_t SysInterlockedExchange(interlockedInt_t& value, interlockedInt_t exchange) { return InterlockedExchange(&value, exchange); }
        interlockedInt_t SysInterlockedCompareExchange(interlockedInt_t& value, interlockedInt_t comparand, interlockedInt_t exchange) { return InterlockedCompareExchange(&value, exchange, comparand); }

        void* SysInterlockedExchangePointer(void*& ptr, void* exchange) { return InterlockedExchangePointer(&ptr, exchange); }
        void* SysInterlockedCompareExchangePointer(void*& ptr, void* comparand, void* exchange) { return InterlockedCompareExchangePointer(&ptr, exchange, comparand); }
    } // namespace cthread

} // namespace cjobs