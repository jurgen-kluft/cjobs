#ifndef __CJOBS_THREADING_H__
#define __CJOBS_THREADING_H__

namespace cjobs
{
    namespace csys
    {
        enum EPriority
        {
            PRIORITY_LOWEST,
            PRIORITY_BELOW_NORMAL,
            PRIORITY_NORMAL,
            PRIORITY_ABOVE_NORMAL,
            PRIORITY_HIGHEST
        };

        typedef int                int32;
        typedef unsigned int       uint32;
        typedef long long          int64;
        typedef unsigned long long uint64;
        typedef int32              core_t;

        typedef int32  interlockedInt_t;
        typedef uint64 signalHandle_t;
        typedef uint64 mutexHandle_t;
        typedef uint64 threadHandle_t;
        typedef uint32 threadId_t;

        threadId_t     SysGetCurrentThreadID();
        threadHandle_t SysGetCurrentThread();

        typedef int32 (*ThreadFunc_t)(void*);

        threadHandle_t SysCreateThread(ThreadFunc_t function, void* parms, EPriority priority, const char* name, core_t core, int32 stackSize = 0, bool suspended = false);
        void           SysWaitForThread(threadHandle_t threadHandle);
        void           SysDestroyThread(threadHandle_t threadHandle);
        bool           SysSetThreadName(threadHandle_t threadHandle, const char* name);

        void SysSignalCreate(signalHandle_t& handle, bool manualReset);
        void SysSignalDestroy(signalHandle_t& handle);
        void SysSignalRaise(signalHandle_t& handle);
        void SysSignalClear(signalHandle_t& handle);
        bool SysSignalWait(signalHandle_t& handle, int32 timeout);

        void SysMutexCreate(mutexHandle_t& handle);
        void SysMutexDestroy(mutexHandle_t& handle);
        bool SysMutexLock(mutexHandle_t& handle, bool blocking);
        void SysMutexUnlock(mutexHandle_t& handle);

        interlockedInt_t SysInterlockedIncrement(interlockedInt_t& value);
        interlockedInt_t SysInterlockedDecrement(interlockedInt_t& value);

        interlockedInt_t SysInterlockedAdd(interlockedInt_t& value, interlockedInt_t i);
        interlockedInt_t SysInterlockedSub(interlockedInt_t& value, interlockedInt_t i);

        interlockedInt_t SysInterlockedExchange(interlockedInt_t& value, interlockedInt_t exchange);
        interlockedInt_t SysInterlockedCompareExchange(interlockedInt_t& value, interlockedInt_t comparand, interlockedInt_t exchange);

        void* SysInterlockedExchangePointer(void*& ptr, void* exchange);
        void* SysInterlockedCompareExchangePointer(void*& ptr, void* comparand, void* exchange);

        void SysYield();

        const int32 MAX_CRITICAL_SECTIONS = 4;

        enum
        {
            CRITICAL_SECTION_ZERO = 0,
            CRITICAL_SECTION_ONE,
            CRITICAL_SECTION_TWO,
            CRITICAL_SECTION_THREE
        };

        class SysMutex
        {
        public:
            SysMutex()
                : mHandle(0)
            {
                SysMutexCreate(mHandle);
            }
            ~SysMutex() { SysMutexDestroy(mHandle); }

            bool Lock(bool blocking = true) { return SysMutexLock(mHandle, blocking); }
            void Unlock() { return SysMutexUnlock(mHandle); }

        protected:
            mutexHandle_t mHandle;

        private:
            SysMutex(const SysMutex& s)
                : mHandle(0)
            {
            }
            void operator=(const SysMutex& s) {}
        };

        class ScopedCriticalSection
        {
        public:
            ScopedCriticalSection(SysMutex& m)
                : mutex(&m)
            {
                mutex->Lock();
            }
            ~ScopedCriticalSection() { mutex->Unlock(); }

        private:
            SysMutex* mutex;
        };

        class SysSignal
        {
        public:
            static const int32 WAIT_INFINITE = -1;

            SysSignal(bool manualReset = false)
                : mHandle(0)
            {
                SysSignalCreate(mHandle, manualReset);
            }
            ~SysSignal() { SysSignalDestroy(mHandle); }

            void Raise() { SysSignalRaise(mHandle); }
            void Clear() { SysSignalClear(mHandle); }

            // Wait returns true if the object is in a signalled state and
            // returns false if the wait timed out. Wait also clears the signalled
            // state when the signalled state is reached within the time out period.
            bool Wait(int32 timeout = WAIT_INFINITE) { return SysSignalWait(mHandle, timeout); }

        protected:
            signalHandle_t mHandle;

        private:
            SysSignal(const SysSignal& s)
                : mHandle(0)
            {
            }
            void operator=(const SysSignal& s) {}
        };

        class SysInterlockedInteger
        {
        public:
            SysInterlockedInteger()
                : mInt(0)
            {
            }

            int32 Increment() { return SysInterlockedIncrement(mInt); }
            int32 Decrement() { return SysInterlockedDecrement(mInt); }

            int32 Add(int32 v) { return SysInterlockedAdd(mInt, (interlockedInt_t)v); }
            int32 Sub(int32 v) { return SysInterlockedSub(mInt, (interlockedInt_t)v); }
            int32 GetValue() const { return mInt; }
            void  SetValue(int32 v) { mInt = (interlockedInt_t)v; }

        private:
            interlockedInt_t mInt;
        };

        struct SysWorkerThreadDescr
        {
            SysWorkerThreadDescr()
                : Name(nullptr)
                , Core(0)
                , Priority(PRIORITY_NORMAL)
                , StackSize(0)
            {
            }
            const char* Name;
            int32       Core;
            EPriority   Priority;
            int32       StackSize;
        };

        class SysWorkerThread
        {
        public:
            SysWorkerThread();
            virtual ~SysWorkerThread();

            const char*    GetName() const { return mDescr.Name; }
            threadHandle_t GetThreadHandle() const { return mThreadHandle; }
            bool           IsRunning() const { return mIsRunning; }
            bool           IsTerminating() const { return mIsTerminating; }

            bool StartThread(SysWorkerThreadDescr descr);
            void StopThread(bool wait = true);

            // This can be called from multiple other threads. However, in the case
            // of a worker thread, the work being "done" has little meaning if other
            // threads are continuously signalling more work.
            void WaitForThread();

            // Signals the thread to notify work is available.
            // This can be called from multiple other threads.
            void SignalWork();

            // Returns true if the work is done without waiting.
            // This can be called from multiple other threads. However, the work
            // being "done" has little meaning if other threads are continuously
            // signalling more work.
            bool IsWorkDone();

        protected:
            // The routine that performs the work.
            virtual int32 Run();

        private:
            SysWorkerThreadDescr mDescr;
            threadHandle_t       mThreadHandle;
            bool                 mIsRunning;
            volatile bool        mIsTerminating;
            volatile bool        mMoreWorkToDo;
            SysSignal            mSignalWorkerDone;
            SysSignal            mSignalMoreWorkToDo;
            SysMutex             mSignalMutex;

            static int32 ThreadProc(SysWorkerThread* thread);

            void operator=(const SysWorkerThread& s) {}
        };

    } // namespace csys
} // namespace cjobs

#endif // __CJOBS_THREADING_H__