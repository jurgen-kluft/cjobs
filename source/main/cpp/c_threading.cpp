#include "cjobs/private/c_threading.h"

#include <string.h>

namespace cjobs
{
    namespace cthread
    {
        SysThread::SysThread()
            : threadHandle(0)
            , isWorker(false)
            , isRunning(false)
            , isTerminating(false)
            , moreWorkToDo(false)
            , signalWorkerDone(true)
        {
        }

        SysThread::~SysThread()
        {
            StopThread(true);
            if (threadHandle)
            {
                DestroyThread(threadHandle);
            }
        }

        bool SysThread::StartThread(const char* name_, core_t core, EPriority priority, int32 stackSize)
        {
            if (isRunning)
            {
                return false;
            }

            strncpy(name, name_, sizeof(name));
            isTerminating = false;

            if (threadHandle)
            {
                DestroyThread(threadHandle);
            }

            threadHandle = CreateThread((ThreadFunc_t)ThreadProc, this, priority, name, core, stackSize, false);

            isRunning = true;
            return true;
        }


        bool SysThread::StartWorkerThread(const char* name_, core_t core, EPriority priority, int32 stackSize)
        {
            if (isRunning)
            {
                return false;
            }

            isWorker = true;

            bool result = StartThread(name_, core, priority, stackSize);

            signalWorkerDone.Wait(SysSignal::WAIT_INFINITE);

            return result;
        }

        void SysThread::StopThread(bool wait)
        {
            if (!isRunning)
            {
                return;
            }

            if (isWorker)
            {
                signalMutex.Lock();
                moreWorkToDo = true;
                signalWorkerDone.Clear();
                isTerminating = true;
                signalMoreWorkToDo.Raise();
                signalMutex.Unlock();
            }
            else
            {
                isTerminating = true;
            }

            if (wait)
            {
                WaitForThread();
            }
        }

        void SysThread::WaitForThread()
        {
            if (isWorker)
            {
                signalWorkerDone.Wait(SysSignal::WAIT_INFINITE);
            }
            else if (isRunning)
            {
                DestroyThread(threadHandle);
                threadHandle = 0;
            }
        }

        void SysThread::SignalWork()
        {
            if (isWorker)
            {
                signalMutex.Lock();
                moreWorkToDo = true;
                signalWorkerDone.Clear();
                signalMoreWorkToDo.Raise();
                signalMutex.Unlock();
            }
        }

        bool SysThread::IsWorkDone()
        {
            if (isWorker)
            {
                // a timeout of 0 will return immediately with true if signaled
                if (signalWorkerDone.Wait(0))
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
                if (thread->isWorker)
                {
                    for (;;)
                    {
                        thread->signalMutex.Lock();
                        if (thread->moreWorkToDo)
                        {
                            thread->moreWorkToDo = false;
                            thread->signalMoreWorkToDo.Clear();
                            thread->signalMutex.Unlock();
                        }
                        else
                        {
                            thread->signalWorkerDone.Raise();
                            thread->signalMutex.Unlock();
                            thread->signalMoreWorkToDo.Wait(SysSignal::WAIT_INFINITE);
                            continue;
                        }

                        if (thread->isTerminating)
                        {
                            break;
                        }

                        retVal = thread->Run();
                    }
                    thread->signalWorkerDone.Raise();
                }
                else
                {
                    retVal = thread->Run();
                }
            }
            
            thread->isRunning = false;
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