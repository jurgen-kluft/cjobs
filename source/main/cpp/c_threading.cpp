#include "cjobs/private/c_threading.h"

#include <string.h>

namespace cjobs
{
    namespace cthread
    {
        SysThread::SysThread()
            : mThreadHandle(0)
            , mIsWorker(false)
            , mIsRunning(false)
            , mIsTerminating(false)
            , mMoreWorkToDo(false)
            , mSignalWorkerDone(true)
        {
        }

        SysThread::~SysThread()
        {
            StopThread(true);
            if (mThreadHandle)
            {
                SysDestroyThread(mThreadHandle);
            }
        }

        bool SysThread::StartThread(const char* name_, core_t core, EPriority priority, int32 stackSize)
        {
            if (mIsRunning)
            {
                return false;
            }

            strncpy(mName, name_, sizeof(mName));
            mIsTerminating = false;

            if (mThreadHandle)
            {
                SysDestroyThread(mThreadHandle);
            }

            mThreadHandle = SysCreateThread((ThreadFunc_t)ThreadProc, this, priority, mName, core, stackSize, false);

            mIsRunning = true;
            return true;
        }


        bool SysThread::StartWorkerThread(const char* name_, core_t core, EPriority priority, int32 stackSize)
        {
            if (mIsRunning)
            {
                return false;
            }

            mIsWorker = true;

            bool result = StartThread(name_, core, priority, stackSize);

            mSignalWorkerDone.Wait(SysSignal::WAIT_INFINITE);

            return result;
        }

        void SysThread::StopThread(bool wait)
        {
            if (!mIsRunning)
            {
                return;
            }

            if (mIsWorker)
            {
                mSignalMutex.Lock();
                mMoreWorkToDo = true;
                mSignalWorkerDone.Clear();
                mIsTerminating = true;
                mSignalMoreWorkToDo.Raise();
                mSignalMutex.Unlock();
            }
            else
            {
                mIsTerminating = true;
            }

            if (wait)
            {
                WaitForThread();
            }
        }

        void SysThread::WaitForThread()
        {
            if (mIsWorker)
            {
                mSignalWorkerDone.Wait(SysSignal::WAIT_INFINITE);
            }
            else if (mIsRunning)
            {
                SysDestroyThread(mThreadHandle);
                mThreadHandle = 0;
            }
        }

        void SysThread::SignalWork()
        {
            if (mIsWorker)
            {
                mSignalMutex.Lock();
                mMoreWorkToDo = true;
                mSignalWorkerDone.Clear();
                mSignalMoreWorkToDo.Raise();
                mSignalMutex.Unlock();
            }
        }

        bool SysThread::IsWorkDone()
        {
            if (mIsWorker)
            {
                // a timeout of 0 will return immediately with true if signaled
                if (mSignalWorkerDone.Wait(0))
                {
                    return true;
                }
            }
            return false;
        }

        int32 SysThread::ThreadProc(SysThread* thread)
        {
            int32 retVal = 0;

            {
                if (thread->mIsWorker)
                {
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
                }
                else
                {
                    retVal = thread->Run();
                }
            }
            
            thread->mIsRunning = false;
            return retVal;
        }

        int32 SysThread::Run()
        {
            // The Run() is not pure virtual because on destruction of a derived class
            // the virtual function pointer will be set to NULL before the SysThread
            // destructor actually stops the thread.
            return 0;
        }

    } // namespace cthread
} // namespace cjobs