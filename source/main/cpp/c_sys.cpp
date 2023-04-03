#include "cjobs/private/c_sys.h"

#include <string.h>

namespace cjobs
{
    namespace csys
    {
        enum EConfig
        {
            DEFAULT_THREAD_STACK_SIZE = (256 * 1024)
        };

        SysWorkerThread::SysWorkerThread()
            : mThreadHandle(0)
            , mIsRunning(false)
            , mIsTerminating(false)
            , mMoreWorkToDo(false)
            , mSignalWorkerDone(true)
        {

        }

        SysWorkerThread::~SysWorkerThread()
        {
            StopThread(true);
            if (mThreadHandle)
            {
                SysDestroyThread(mThreadHandle);
            }
        }

        bool SysWorkerThread::StartThread(SysWorkerThreadDescr descr)
        {
            if (mIsRunning)
            {
                return false;
            }

            mIsTerminating = false;

            if (mThreadHandle)
            {
                SysDestroyThread(mThreadHandle);
            }

            mDescr = descr;
            
            int32 stackSize = descr.StackSize;
            if (stackSize == 0)
                stackSize = DEFAULT_THREAD_STACK_SIZE;

            mThreadHandle = SysCreateThread((ThreadFunc_t)ThreadProc, this, descr.Priority, descr.Name, descr.Core, stackSize, false);
            mIsRunning = true;

            mSignalWorkerDone.Wait(SysSignal::WAIT_INFINITE);

            return true;
        }

        void SysWorkerThread::StopThread(bool wait)
        {
            if (!mIsRunning)
            {
                return;
            }

            mSignalMutex.Lock();
            mMoreWorkToDo = true;
            mSignalWorkerDone.Clear();
            mIsTerminating = true;
            mSignalMoreWorkToDo.Raise();
            mSignalMutex.Unlock();

            if (wait)
            {
                WaitForThread();
            }
        }

        void SysWorkerThread::WaitForThread()
        {
            mSignalWorkerDone.Wait(SysSignal::WAIT_INFINITE);
        }

        void SysWorkerThread::SignalWork()
        {
            mSignalMutex.Lock();
            mMoreWorkToDo = true;
            mSignalWorkerDone.Clear();
            mSignalMoreWorkToDo.Raise();
            mSignalMutex.Unlock();
        }

        bool SysWorkerThread::IsWorkDone()
        {
            // a timeout of 0 will return immediately with true if signaled
            if (mSignalWorkerDone.Wait(0))
            {
                return true;
            }
            return false;
        }

        int32 SysWorkerThread::ThreadProc(SysWorkerThread* thread)
        {
            int32 retVal = 0;

            for (;;)
            {
                thread->mSignalMutex.Lock();
                if (thread->mMoreWorkToDo)
                {
                    thread->mMoreWorkToDo = false;
                    thread->mSignalMoreWorkToDo.Clear();
                    thread->mSignalMutex.Unlock();
                }
                else
                {
                    thread->mSignalWorkerDone.Raise();
                    thread->mSignalMutex.Unlock();
                    thread->mSignalMoreWorkToDo.Wait(SysSignal::WAIT_INFINITE);
                    continue;
                }

                if (thread->mIsTerminating)
                {
                    break;
                }

                retVal = thread->Run();
            }

            thread->mSignalWorkerDone.Raise();
            thread->mIsRunning = false;
            return retVal;
        }

        int32 SysWorkerThread::Run()
        {
            // The Run() is not a pure virtual function because on destruction of a derived
            // class the virtual function pointer will be set to NULL before the SysWorkerThread
            // destructor actually stops the thread.
            return 0;
        }

    } // namespace cthread
} // namespace cjobs