#ifndef __CJOBS_THREADING_H__
#define __CJOBS_THREADING_H__

namespace cjobs
{
    namespace cthread
    {
        enum EConfig
        {
            DEFAULT_THREAD_STACK_SIZE = (256 * 1024)
        };

        enum EPriority
        {
            PRIORITY_LOWEST,
            PRIORITY_BELOW_NORMAL,
            PRIORITY_NORMAL,
            PRIORITY_ABOVE_NORMAL,
            PRIORITY_HIGHEST
        };

        typedef int32              int32;
        typedef unsigned int     uint32;
        typedef long long          int64;
        typedef unsigned long long uint64;
        typedef uint64             threadHandle_t;
        typedef int32              core_t;

        typedef int64  interlockedInt_t;
        typedef uint64 signalHandle_t;
        typedef uint64 mutexHandle_t;

        typedef uint32 (*ThreadFunc_t)(void*);

        // on win32, the threadID is NOT the same as the threadHandle
        threadHandle_t GetCurrentThreadID();

        // returns a threadHandle
        threadHandle_t CreateThread(ThreadFunc_t function, void* parms, EPriority priority, const char* name, core_t core, int32 stackSize = DEFAULT_THREAD_STACK_SIZE, bool suspended = false);

        void WaitForThread(threadHandle_t threadHandle);
        void DestroyThread(threadHandle_t threadHandle);
        void SetCurrentThreadName(const char* name);

        void SignalCreate(signalHandle_t& handle, bool manualReset);
        void SignalDestroy(signalHandle_t& handle);
        void SignalRaise(signalHandle_t& handle);
        void SignalClear(signalHandle_t& handle);
        bool SignalWait(signalHandle_t& handle, int32 timeout);

        void MutexCreate(mutexHandle_t& handle);
        void MutexDestroy(mutexHandle_t& handle);
        bool MutexLock(mutexHandle_t& handle, bool blocking);
        void MutexUnlock(mutexHandle_t& handle);

        interlockedInt_t InterlockedIncrement(interlockedInt_t& value);
        interlockedInt_t InterlockedDecrement(interlockedInt_t& value);

        interlockedInt_t InterlockedAdd(interlockedInt_t& value, interlockedInt_t i);
        interlockedInt_t InterlockedSub(interlockedInt_t& value, interlockedInt_t i);

        interlockedInt_t InterlockedExchange(interlockedInt_t& value, interlockedInt_t exchange);
        interlockedInt_t InterlockedCompareExchange(interlockedInt_t& value, interlockedInt_t comparand, interlockedInt_t exchange);

        void* InterlockedExchangePointer(void*& ptr, void* exchange);
        void* InterlockedCompareExchangePointer(void*& ptr, void* comparand, void* exchange);

        void Yield();

        void   CPUCount(int32& logicalNum, int32& coreNum, int32& packageNum);
        core_t ThreadToCore(int32 thread);

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
            SysMutex() { MutexCreate(mHandle); }
            ~SysMutex() { MutexDestroy(mHandle); }

            bool Lock(bool blocking = true) { return MutexLock(mHandle, blocking); }
            void Unlock() { return MutexUnlock(mHandle); }

        protected:
            mutexHandle_t mHandle;

        private:
            SysMutex(const SysMutex& s) {}
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

            SysSignal(bool manualReset = false) { SignalCreate(mHandle, manualReset); }
            ~SysSignal() { SignalDestroy(mHandle); }

            void Raise() { SignalRaise(mHandle); }
            void Clear() { SignalClear(mHandle); }

            // Wait returns true if the object is in a signalled state and
            // returns false if the wait timed out. Wait also clears the signalled
            // state when the signalled state is reached within the time out period.
            bool Wait(int32 timeout = WAIT_INFINITE) { return SignalWait(mHandle, timeout); }

        protected:
            signalHandle_t mHandle;

        private:
            SysSignal(const SysSignal& s) {}
            void operator=(const SysSignal& s) {}
        };

        class SysInterlockedInteger
        {
        public:
            SysInterlockedInteger()
                : mInt(0)
            {
            }

            int32 Increment() { return InterlockedIncrement(mInt); }
            int32 Decrement() { return InterlockedDecrement(mInt); }

            int32 Add(int32 v) { return InterlockedAdd(mInt, (interlockedInt_t)v); }
            int32 Sub(int32 v) { return InterlockedSub(mInt, (interlockedInt_t)v); }
            int32 GetValue() const { return mInt; }
            void  SetValue(int32 v) { mInt = (interlockedInt_t)v; }

        private:
            interlockedInt_t mInt;
        };

        class SysThread
        {
        public:
            SysThread();
            virtual ~SysThread();

            const char*    GetName() const { return name; }
            threadHandle_t GetThreadHandle() const { return threadHandle; }
            bool           IsRunning() const { return isRunning; }
            bool           IsTerminating() const { return isTerminating; }

            bool StartThread(const char* name, core_t core, EPriority priority = PRIORITY_NORMAL, int32 stackSize = DEFAULT_THREAD_STACK_SIZE);
            bool StartWorkerThread(const char* name, core_t core, EPriority priority = PRIORITY_NORMAL, int32 stackSize = DEFAULT_THREAD_STACK_SIZE);
            void StopThread(bool wait = true);

            // This can be called from multiple other threads. However, in the case
            // of a worker thread, the work being "done" has little meaning if other
            // threads are continuously signalling more work.
            void WaitForThread();

            //------------------------
            // Worker Thread
            //------------------------

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
            char           name[32];
            threadHandle_t threadHandle;
            bool           isWorker;
            bool           isRunning;
            volatile bool  isTerminating;
            volatile bool  moreWorkToDo;
            SysSignal      signalWorkerDone;
            SysSignal      signalMoreWorkToDo;
            SysMutex       signalMutex;

            static int32 ThreadProc(SysThread* thread);

            SysThread(const SysThread& s) {}
            void operator=(const SysThread& s) {}
        };

    } // namespace cthread
} // namespace cjobs

#endif // __CJOBS_THREADING_H__mHandle