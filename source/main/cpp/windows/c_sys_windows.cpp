#include "cjobs/c_jobs.h"
#include "cjobs/private/c_sys.h"

#if defined(_MSC_VER) && !defined(__clang__) && !defined(__GNUC__) && !defined(NOMINMAX)
#define NOMINMAX
#endif

#include <windows.h>


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

    typedef uint64 ticks_t; 
    static double sTimerFrequency = 0;

    void SysTimerInit()
    {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        sTimerFrequency = static_cast<double>(frequency.QuadPart);
    }

    ticks_t SysTimerCurrent()
    {
        LARGE_INTEGER ticks;
        QueryPerformanceCounter(&ticks);
        return static_cast<ticks_t>(ticks.QuadPart);
    }

    ticks_t SysTimerLap(ticks_t time)
    {
        LARGE_INTEGER ticks;
        QueryPerformanceCounter(&ticks);
        ticks_t current = static_cast<ticks_t>(ticks.QuadPart);
        return current - time;
    }

    double SysTimerElapsedMs(ticks_t time)
    {
        LARGE_INTEGER ticks;
        QueryPerformanceCounter(&ticks);
        return (static_cast<double>(ticks.QuadPart - time) * 1000.0) / sTimerFrequency;
    }

    uint64 g_Microseconds()
    {
        double ms = SysTimerElapsedMs(0);
        return static_cast<uint64>(ms * 1000.0);
    }

    namespace csys
    {
        bool SysSetThreadName(threadHandle_t handle, const char* name)
        {
            wchar_t wname[64];
            MultiByteToWideChar(CP_UTF8, MB_PRECOMPOSED, name, -1, wname, _countof(wname)-1);

            HRESULT hr = SetThreadDescription((HANDLE)handle, wname);
            if (FAILED(hr))
            {
                return false;
            }
            return true;
        }

        threadHandle_t SysCreateThread(ThreadFunc_t function, void* parms, EPriority priority, const char* name, core_t core, int32 stackSize, bool suspended)
        {
            DWORD flags = (suspended ? CREATE_SUSPENDED : 0);
            flags |= STACK_SIZE_PARAM_IS_A_RESERVATION;

            DWORD  threadId;
            HANDLE handle = ::CreateThread(NULL, stackSize, (LPTHREAD_START_ROUTINE)function, parms, flags, &threadId);
            if (handle == 0)
            {
                // Error("CreateThread error: %i", GetLastError());
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
            // SetThreadAffinityMask((HANDLE)handle, 1<<core);

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

        void SysSignalCreate(signalHandle_t& handle, bool manualReset) { handle = (signalHandle_t)CreateEvent(NULL, manualReset, FALSE, NULL); }
        void SysSignalDestroy(signalHandle_t& handle) { CloseHandle((HANDLE)handle); }
        void SysSignalRaise(signalHandle_t& handle) { SetEvent((HANDLE)handle); }
        void SysSignalClear(signalHandle_t& handle)
        {
            // events are created as auto-reset so this should never be needed
            ResetEvent((HANDLE)handle);
        }

        bool SysSignalWait(signalHandle_t& handle, int32 timeout)
        {
            DWORD result = WaitForSingleObject((HANDLE)handle, timeout == SysSignal::WAIT_INFINITE ? INFINITE : timeout);

//            assert(result == WAIT_OBJECT_0 || (timeout != SysSignal::WAIT_INFINITE && result == WAIT_TIMEOUT));

            return (result == WAIT_OBJECT_0);
        }

        void SysMutexCreate(mutexHandle_t& handle) { InitializeCriticalSection((LPCRITICAL_SECTION)&handle); }
        void SysMutexDestroy(mutexHandle_t& handle) { DeleteCriticalSection((LPCRITICAL_SECTION)&handle); }

        bool SysMutexLock(mutexHandle_t& handle, bool blocking)
        {
            if (TryEnterCriticalSection((LPCRITICAL_SECTION)&handle) == 0)
            {
                if (!blocking)
                {
                    return false;
                }
                EnterCriticalSection((LPCRITICAL_SECTION)&handle);
            }
            return true;
        }

        void SysMutexUnlock(mutexHandle_t& handle) { LeaveCriticalSection((LPCRITICAL_SECTION)&handle); }       

        typedef LONG volatile* pvAtomicInt32;

        interlockedInt_t SysInterlockedIncrement(interlockedInt_t& value) { return InterlockedIncrementAcquire((pvAtomicInt32)&value); }
        interlockedInt_t SysInterlockedDecrement(interlockedInt_t& value) { return InterlockedDecrementRelease((pvAtomicInt32)&value); }
        interlockedInt_t SysInterlockedAdd(interlockedInt_t& value, interlockedInt_t i) { return InterlockedExchangeAdd((pvAtomicInt32)&value, i) + i; }
        interlockedInt_t SysInterlockedSub(interlockedInt_t& value, interlockedInt_t i) { return InterlockedExchangeAdd((pvAtomicInt32)&value, -i) - i; }
        interlockedInt_t SysInterlockedExchange(interlockedInt_t& value, interlockedInt_t exchange) { return InterlockedExchange((pvAtomicInt32)&value, exchange); }
        interlockedInt_t SysInterlockedCompareExchange(interlockedInt_t& value, interlockedInt_t comparand, interlockedInt_t exchange) { return InterlockedCompareExchange((pvAtomicInt32)&value, exchange, comparand); }

        void* SysInterlockedExchangePointer(void*& ptr, void* exchange) { return InterlockedExchangePointer(&ptr, exchange); }
        void* SysInterlockedCompareExchangePointer(void*& ptr, void* comparand, void* exchange) { return InterlockedCompareExchangePointer(&ptr, exchange, comparand); }
    } // namespace cthread

} // namespace cjobs