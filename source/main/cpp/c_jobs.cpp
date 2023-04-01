#include "cjobs/c_jobs.h"
#include "cjobs/private/c_sys.h"

#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace cjobs
{
    typedef csys::SysInterlockedInteger InterlockedInt;

    //-----------------------------------------------------------------------------
    template <typename T, typename... Args> inline T* Construct(Alloc* a, int32 tag, Args&&... args)
    {
        void* mem = a->Allocate(sizeof(T), sizeof(void*), tag));
        return new (mem) T((args...);
    }

    template <typename T> inline void Destruct(Alloc* a, T* ptr)
    {
        ptr->~T();
        a->Deallocate(ptr);
    }

    template <typename T> class List
    {
    public:
        List()
            : mAlloc(nullptr)
            , mData(nullptr)
            , mNum(0)
            , mCapacity(0)
        {
        }

        void Init(Alloc* alloc);

        bool IsEmpty() const { return mNum == 0; }
        bool IsFull() const { return mNum == mCapacity; }

        int32 Num() const { return mNum; }
        void  SetNum(int32 n) { mNum = n; }
        void  SetCapacity(int32 n);

        T&    Allocate();
        void  Append(const T& item);
        int32 FindIndex(const T& item) const;
        void  RemoveIndexFast(int32 index);

        inline T& operator[](int32 index) { return mData[index]; }
        inline T  operator[](int32 index) const { return mData[index]; }

    protected:
        Alloc* mAlloc;
        T*     mData;
        int32  mNum;
        int32  mCapacity;
    };

    extern uint64 g_Microseconds();

    extern void g_Printf(const char* format, ...);
    extern void g_Error(const char* format, ...);
    extern void g_AssertFailed(const char* file, int line, const char* expression);

#define DEBUG_ASSERT(x)                         \
    if (!(x))                                   \
    {                                           \
        g_AssertFailed(__FILE__, __LINE__, #x); \
    }

#define DEBUG_VERIFY(x) ((x) ? true : (g_AssertFailed(__FILE__, __LINE__, #x), false))

    char* CopyStr(char* dst, const char* dstEnd, const char* src)
    {
        while (dst < dstEnd && *src != 0)
        {
            *dst++ = *src++;
        }
        *dst = 0;
        return dst;
    }

    char* AppendDecimalToStr(char*& dst, const char* dstEnd, int32 value, int32 maxdigits)
    {
        int32 i = 0;
        while (i < maxdigits && dst < dstEnd)
        {
            *dst++ = '0' + (value % 10);
            value /= 10;
            i++;
        }
        *dst = 0;
        return dst;
    }

    struct JobsListState_t
    {
        JobsListState_t()
            : mJobsList(nullptr)
            , mVersion(0xFFFFFFFF)
            , mSignalIndex(0)
            , mLastJobIndex(0)
            , mNextJobIndex(-1)
        {
        }
        JobsListState_t(int32 _version)
            : mJobsList(nullptr)
            , mVersion(_version)
            , mSignalIndex(0)
            , mLastJobIndex(0)
            , mNextJobIndex(-1)
        {
        }
        JobsListInstance* mJobsList;
        int32             mVersion;
        int32             mSignalIndex;
        int32             mLastJobIndex;
        int32             mNextJobIndex;
    };

    class JobsRegister
    {
    public:
        JobsRegister();
        ~JobsRegister();

        int32       Count() const;
        void        RegisterJob(JobRun_t function, const char* name);
        bool        IsRegisteredJob(JobRun_t function) const;
        const char* GetJobNameByIndex(int32 index) const;
        const char* GetJobNameByFunction(JobRun_t function) const;

    protected:
        int32       mNumRegisteredJobs;
        JobRun_t    mRegisteredJobFuncs[CONFIG_MAX_REGISTERED_JOBS];
        const char* mRegisteredJobNames[CONFIG_MAX_REGISTERED_JOBS];
    };

    class JobsListInstance
    {
    public:
        JobsListInstance(Alloc* allocator, JobsListId_t id, JobsListDescr const& descr);
        ~JobsListInstance();

        // These are called from the one thread that manages this list.
        inline void AddJob(JobRun_t function, void* data);
        inline void InsertSyncPoint(EJobsSyncType_t syncType);
        void        Submit(JobsListInstance* waitForJobList_, int32 parallelism);
        void        Wait();
        bool        TryWait();
        bool        IsSubmitted() const;

        JobsListId_t         GetId() const { return mListId; }
        JobsListDescr const& GetDescr() const { return mDescr; }
        EJobsListPriority_t  GetPriority() const { return mDescr.Priority; }
        int32                GetVersion() { return mVersion.GetValue(); }
        ThreadStats_t*       GetThreadStats() { return &mThreadStats; }
        ThreadStats_t const* GetThreadStats() const { return &mThreadStats; }

        bool WaitForOtherJobsList();

        // This is thread safe and called from the job mThreads.
        enum runResult_t
        {
            RUN_OK       = 0,
            RUN_PROGRESS = 1 << 0,
            RUN_DONE     = 1 << 1,
            RUN_STALLED  = 1 << 2
        };

        int32 RunJobs(uint32 mThreadNum, JobsListState_t& state, bool singleJob);

        static const int32 NUM_DONE_GUARDS = 4; // cycle through 4 guards so we can cyclicly chain job lists

        Alloc*          mAllocator;
        bool            mThreaded;
        bool            mDone;
        bool            mHasSignal;
        JobsListId_t    mListId;
        JobsListDescr   mDescr;
        uint32          mNumSyncs;
        int32           mLastSignalJob;
        InterlockedInt* mWaitForGuard;
        InterlockedInt  mDoneGuards[NUM_DONE_GUARDS];
        int32           mCurrentDoneGuard;
        InterlockedInt  mVersion;
        struct job_t
        {
            JobRun_t function;
            void*    data;
            int32    executed;
        };
        int32                mJobListIndexDebug;
        List<job_t>          mJobsList;
        List<InterlockedInt> mSignalJobCount;
        InterlockedInt       mCurrentJob;
        InterlockedInt       mFetchLock;
        InterlockedInt       mNumThreadsExecuting;

        JobsRegister* mJobsRegister;

        ThreadStats_t mDeferredThreadStats;
        ThreadStats_t mThreadStats;

        int32 RunJobsInternal(uint32 mThreadNum, JobsListState_t& state, bool singleJob);

        static void Nop(void* data) {}

        static int32 JOB_SIGNAL;
        static int32 JOB_SYNCHRONIZE;
        static int32 JOB_LIST_DONE;
    };

    int32 JobsListInstance::JOB_SIGNAL;
    int32 JobsListInstance::JOB_SYNCHRONIZE;
    int32 JobsListInstance::JOB_LIST_DONE;

    JobsListInstance::JobsListInstance(Alloc* allocator, JobsListId_t id, JobsListDescr const& descr)
        : mAllocator(allocator)
        , mThreaded(true)
        , mDone(true)
        , mHasSignal(false)
        , mListId(id)
        , mDescr(descr)
        , mNumSyncs(0)
        , mLastSignalJob(0)
        , mWaitForGuard(nullptr)
        , mCurrentDoneGuard(0)
        , mJobsList()
        , mSignalJobCount()
    {
        DEBUG_ASSERT(mDescr.Priority != JOBSLIST_PRIORITY_NONE);

        mJobsList.Init(mAllocator);
        mJobsList.SetCapacity(mDescr.MaxJobs + mDescr.MaxSyncs * 2 + 1); // syncs go in as dummy jobs and one more to update the doneCount
        mJobsList.SetNum(0);
        mSignalJobCount.Init(mAllocator);
        mSignalJobCount.SetCapacity(mDescr.MaxSyncs + 1); // need one extra for submit
        mSignalJobCount.SetNum(0);

        memset(&mDeferredThreadStats, 0, sizeof(ThreadStats_t));
        memset(&mThreadStats, 0, sizeof(ThreadStats_t));
    }

    JobsListInstance::~JobsListInstance()
    {
        Wait();
        mJobsList.SetCapacity(0);
        mSignalJobCount.SetCapacity(0);
    }

    inline void JobsListInstance::AddJob(JobRun_t function, void* data)
    {
        DEBUG_ASSERT(mDone);

#if defined(PLATFORM_DEBUG)
        // don't check all jobs each time we come here, just check a part and next time another part
        for (int32 i = 0; i < 10; i++)
        {
            mJobListIndexDebug = (mJobListIndexDebug + 1);
            if (mJobListIndexDebug >= mJobsList.Num())
                mJobListIndexDebug = 0;
            DEBUG_ASSERT(mJobsList[mJobListIndexDebug].function != function || mJobsList[mJobListIndexDebug].data != data);
        }
#endif
        if (!mJobsList.IsFull())
        {
            job_t& job   = mJobsList.Allocate();
            job.function = function;
            job.data     = data;
            job.executed = 0;
        }
        else
        {
            // debug output to show us what is overflowing
            int32 currentJobCount[CONFIG_MAX_REGISTERED_JOBS] = {};

            for (int32 i = 0; i < mJobsList.Num(); ++i)
            {
                const char* jobName = mJobsRegister->GetJobNameByFunction(mJobsList[i].function);
                for (int32 j = 0; j < mJobsRegister->Count(); ++j)
                {
                    if (jobName == mJobsRegister->GetJobNameByIndex(j))
                    {
                        currentJobCount[j]++;
                        break;
                    }
                }
            }

            // print the quantity of each job type
            for (int32 i = 0; i < mJobsRegister->Count(); ++i)
            {
                if (currentJobCount[i] > 0)
                {
                    g_Printf("Job: %s, # %d", mJobsRegister->GetJobNameByIndex(i), currentJobCount[i]);
                }
            }
            g_Error("Can't add job '%s', too many jobs %d", mJobsRegister->GetJobNameByFunction(function), mJobsList.Num());
        }
    }

    inline void JobsListInstance::InsertSyncPoint(EJobsSyncType_t syncType)
    {
        DEBUG_ASSERT(mDone);
        switch (syncType)
        {
            case JOBSSYNC_SIGNAL:
            {
                DEBUG_ASSERT(!mHasSignal);
                if (mJobsList.Num())
                {
                    DEBUG_ASSERT(!mHasSignal);
                    mSignalJobCount.Allocate();
                    mSignalJobCount[mSignalJobCount.Num() - 1].SetValue(mJobsList.Num() - mLastSignalJob);
                    mLastSignalJob = mJobsList.Num();
                    job_t& job     = mJobsList.Allocate();
                    job.function   = Nop;
                    job.data       = &JOB_SIGNAL;
                    mHasSignal     = true;
                }
                break;
            }
            case JOBSSYNC_SYNCHRONIZE:
            {
                if (mHasSignal)
                {
                    job_t& job   = mJobsList.Allocate();
                    job.function = Nop;
                    job.data     = &JOB_SYNCHRONIZE;
                    mHasSignal   = false;
                    mNumSyncs++;
                }
                break;
            }
        }
    }

    void JobsListInstance::Submit(JobsListInstance* waitForJobList, int32 parallelism)
    {
        DEBUG_ASSERT(mDone);
        DEBUG_ASSERT(mNumSyncs <= mDescr.MaxSyncs);
        DEBUG_ASSERT((uint32)mJobsList.Num() <= mDescr.MaxJobs + mNumSyncs * 2);
        DEBUG_ASSERT(mFetchLock.GetValue() == 0);

        mDone = false;
        mCurrentJob.SetValue(0);

        memset(&mDeferredThreadStats, 0, sizeof(mDeferredThreadStats));
        mDeferredThreadStats.mNumExecutedJobs  = mJobsList.Num() - mNumSyncs * 2;
        mDeferredThreadStats.mNumExecutedSyncs = mNumSyncs;
        mDeferredThreadStats.mSubmitTime       = g_Microseconds();
        mDeferredThreadStats.mStartTime        = 0;
        mDeferredThreadStats.mEndTime          = 0;
        mDeferredThreadStats.mWaitTime         = 0;

        if (mJobsList.Num() == 0)
        {
            return;
        }

        if (waitForJobList != nullptr)
        {
            mWaitForGuard = &waitForJobList->mDoneGuards[waitForJobList->mCurrentDoneGuard];
        }
        else
        {
            mWaitForGuard = nullptr;
        }

        mCurrentDoneGuard = (mCurrentDoneGuard + 1) & (NUM_DONE_GUARDS - 1);
        mDoneGuards[mCurrentDoneGuard].SetValue(1);

        mSignalJobCount.Allocate();
        mSignalJobCount[mSignalJobCount.Num() - 1].SetValue(mJobsList.Num() - mLastSignalJob);

        job_t& job   = mJobsList.Allocate();
        job.function = Nop;
        job.data     = &JOB_LIST_DONE;

        if (mThreaded)
        {
            // hand over to the manager
            void SubmitJobList(JobsListInstance * mJobsList, int32 parallelism);
            SubmitJobList(this, parallelism);
        }
        else
        {
            // run all the jobs right here
            JobsListState_t state(GetVersion());
            RunJobs(0, state, false);
        }
    }

    void JobsListInstance::Wait()
    {
        if (mJobsList.Num() > 0)
        {
            // don't lock up but return if the job list was never properly submitted
            if (!DEBUG_VERIFY(!mDone && mSignalJobCount.Num() > 0))
            {
                return;
            }

            bool   waited    = false;
            uint64 waitStart = g_Microseconds();

            while (mSignalJobCount[mSignalJobCount.Num() - 1].GetValue() > 0)
            {
                csys::SysYield();
                waited = true;
            }
            mVersion.Increment();
            while (mNumThreadsExecuting.GetValue() > 0)
            {
                csys::SysYield();
                waited = true;
            }

            mJobsList.SetNum(0);
            mSignalJobCount.SetNum(0);
            mNumSyncs      = 0;
            mLastSignalJob = 0;

            uint64 waitEnd                 = g_Microseconds();
            mDeferredThreadStats.mWaitTime = waited ? (waitEnd - waitStart) : 0;
        }
        memcpy(&mThreadStats, &mDeferredThreadStats, sizeof(mThreadStats));
        mDone = true;
    }

    bool JobsListInstance::TryWait()
    {
        if (mJobsList.Num() == 0 || mSignalJobCount[mSignalJobCount.Num() - 1].GetValue() <= 0)
        {
            Wait();
            return true;
        }
        return false;
    }

    bool JobsListInstance::IsSubmitted() const { return !mDone; }

#ifdef PLATFORM_DEBUG
    volatile float    longJobTime;
    volatile JobRun_t longJobFunc;
    volatile void*    longJobData;
#endif

    int32 JobsListInstance::RunJobsInternal(uint32 mThreadNum, JobsListState_t& state, bool singleJob)
    {
        if (state.mVersion != mVersion.GetValue())
        {
            // trying to run an old mVersion of this list that is already mDone
            return RUN_DONE;
        }

        DEBUG_ASSERT(mThreadNum < CONFIG_MAX_THREADS);

        if (mDeferredThreadStats.mStartTime == 0)
        {
            mDeferredThreadStats.mStartTime = g_Microseconds(); // first time any thread is running jobs from this list
        }

        int32 result = RUN_OK;

        do
        {
            // run through all signals and syncs before the last job that has been or is being executed
            // this loop is really an optimization to minimize the time spent in the mFetchLock section below
            for (; state.mLastJobIndex < (int32)mCurrentJob.GetValue() && state.mLastJobIndex < mJobsList.Num(); state.mLastJobIndex++)
            {
                if (mJobsList[state.mLastJobIndex].data == &JOB_SIGNAL)
                {
                    state.mSignalIndex++;
                    DEBUG_ASSERT(state.mSignalIndex < mSignalJobCount.Num());
                }
                else if (mJobsList[state.mLastJobIndex].data == &JOB_SYNCHRONIZE)
                {
                    DEBUG_ASSERT(state.mSignalIndex > 0);
                    if (mSignalJobCount[state.mSignalIndex - 1].GetValue() > 0)
                    {
                        // stalled on a synchronization point
                        return (result | RUN_STALLED);
                    }
                }
                else if (mJobsList[state.mLastJobIndex].data == &JOB_LIST_DONE)
                {
                    if (mSignalJobCount[mSignalJobCount.Num() - 1].GetValue() > 0)
                    {
                        // stalled on a synchronization point
                        return (result | RUN_STALLED);
                    }
                }
            }

            // try to lock to fetch a new job
            if (mFetchLock.Increment() == 1)
            {
                // grab a new job
                state.mNextJobIndex = mCurrentJob.Increment() - 1;

                // run through any remaining signals and syncs (this should rarely iterate more than once)
                for (; state.mLastJobIndex <= state.mNextJobIndex && state.mLastJobIndex < mJobsList.Num(); state.mLastJobIndex++)
                {
                    if (mJobsList[state.mLastJobIndex].data == &JOB_SIGNAL)
                    {
                        state.mSignalIndex++;
                        DEBUG_ASSERT(state.mSignalIndex < mSignalJobCount.Num());
                    }
                    else if (mJobsList[state.mLastJobIndex].data == &JOB_SYNCHRONIZE)
                    {
                        DEBUG_ASSERT(state.mSignalIndex > 0);
                        if (mSignalJobCount[state.mSignalIndex - 1].GetValue() > 0)
                        {
                            // return this job to the list
                            mCurrentJob.Decrement();
                            mFetchLock.Decrement();        // release the fetch lock
                            return (result | RUN_STALLED); // stalled on a synchronization point
                        }
                    }
                    else if (mJobsList[state.mLastJobIndex].data == &JOB_LIST_DONE)
                    {
                        if (mSignalJobCount[mSignalJobCount.Num() - 1].GetValue() > 0)
                        {
                            // return this job to the list
                            mCurrentJob.Decrement();
                            mFetchLock.Decrement();        // release the fetch lock
                            return (result | RUN_STALLED); // stalled on a synchronization point
                        }
                        // decrement the mDone count
                        mDoneGuards[mCurrentDoneGuard].Decrement();
                    }
                }
                mFetchLock.Decrement(); // release the fetch lock
            }
            else
            {
                mFetchLock.Decrement();        // release the fetch lock
                return (result | RUN_STALLED); // another thread is fetching right now so consider stalled
            }

            // if at the end of the job list we're mDone
            if (state.mNextJobIndex >= mJobsList.Num())
            {
                return (result | RUN_DONE);
            }

            // execute the next job
            {
                uint64 jobStart = g_Microseconds();

                mJobsList[state.mNextJobIndex].function(mJobsList[state.mNextJobIndex].data);
                mJobsList[state.mNextJobIndex].executed = 1;

                uint64 jobEnd = g_Microseconds();
                mDeferredThreadStats.mThreadExecTime[mThreadNum] += jobEnd - jobStart;

#ifdef PLATFORM_DEBUG
                if (jobs_longJobMicroSec.GetInteger() > 0)
                {
                    if (jobEnd - jobStart > jobs_longJobMicroSec.GetInteger() && GetId() != JOBLIST_UTILITY)
                    {
                        longJobTime             = (jobEnd - jobStart) * (1.0f / 1000.0f);
                        longJobFunc             = mJobsList[state.mNextJobIndex].function;
                        longJobData             = mJobsList[state.mNextJobIndex].data;
                        const char* jobName     = GetJobName(mJobsList[state.mNextJobIndex].function);
                        const char* jobListName = GetJobListName(GetId());
                        g_Printf("%1.1f milliseconds for a single '%s' job from job list %s on thread %d\n", longJobTime, jobName, jobListName, mThreadNum);
                    }
                }
#endif
            }

            result |= RUN_PROGRESS;

            // decrease the job count for the current signal
            if (mSignalJobCount[state.mSignalIndex].Decrement() == 0)
            {
                // if this was the very last job of the job list
                if (state.mSignalIndex == mSignalJobCount.Num() - 1)
                {
                    mDeferredThreadStats.mEndTime = g_Microseconds();
                    return (result | RUN_DONE);
                }
            }

        } while (!singleJob);

        return result;
    }

    int32 JobsListInstance::RunJobs(uint32 mThreadNum, JobsListState_t& state, bool singleJob)
    {
        int32  result = 0;
        uint64 start  = g_Microseconds();

        mNumThreadsExecuting.Increment();
        {
            result = RunJobsInternal(mThreadNum, state, singleJob);
        }
        mNumThreadsExecuting.Decrement();

        mDeferredThreadStats.mThreadTotalTime[mThreadNum] += g_Microseconds() - start;
        return result;
    }

    bool JobsListInstance::WaitForOtherJobsList()
    {
        if (mWaitForGuard != nullptr)
        {
            if (mWaitForGuard->GetValue() > 0)
            {
                return true;
            }
        }
        return false;
    }

    JobsList::JobsList(Alloc* allocator, JobsListId_t id, JobsListDescr const& descr)
        : mJobsListInstance(nullptr)
    {
        DEBUG_ASSERT(descr.Priority > JOBSLIST_PRIORITY_NONE);
        this->mJobsListInstance = Construct<JobsListInstance>(allocator, TAG_JOBLIST, allocator, id, descr);
    }

    JobsList::~JobsList() { Destruct(mJobsListInstance->mAllocator, mJobsListInstance); }

    void JobsList::AddJob(JobRun_t function, void* data)
    {
        DEBUG_ASSERT(this->mJobsListInstance->mJobsRegister->IsRegisteredJob(function));
        mJobsListInstance->AddJob(function, data);
    }

    void JobsList::InsertSyncPoint(EJobsSyncType_t syncType) { mJobsListInstance->InsertSyncPoint(syncType); }

    void JobsList::Wait()
    {
        if (mJobsListInstance != nullptr)
        {
            mJobsListInstance->Wait();
        }
    }

    bool JobsList::TryWait()
    {
        bool mDone = true;
        if (mJobsListInstance != nullptr)
        {
            mDone &= mJobsListInstance->TryWait();
        }
        return mDone;
    }

    void JobsList::Submit(JobsList* waitForJobList, int32 parallelism)
    {
        DEBUG_ASSERT(waitForJobList != this);
        mJobsListInstance->Submit((waitForJobList != nullptr) ? waitForJobList->mJobsListInstance : nullptr, parallelism);
    }

    bool                 JobsList::IsSubmitted() const { return mJobsListInstance->IsSubmitted(); }
    JobsListId_t         JobsList::GetId() const { return mJobsListInstance->GetId(); }
    ThreadStats_t const* JobsList::GetStats() const { return mJobsListInstance->GetThreadStats(); }

    const int32 JOB_THREAD_STACK_SIZE = 256 * 1024; // same size as the SPU local store

    class JobThread : public csys::SysWorkerThread
    {
    public:
        JobThread(JobsThreadDescr descr);
        ~JobThread();

        void Start(uint32 threadNum);
        void AddJobList(JobsListInstance* mJobsList);

    protected:
        JobsThreadDescr   mDescr;
        JobsListInstance* mJobListInstances[CONFIG_MAX_JOBLISTS]; // cyclic buffer with job lists
        int32             mJobListVersions[CONFIG_MAX_JOBLISTS];  // cyclic buffer with job lists
        uint16            mFirstJobList;                          // index of the last job list the thread grabbed
        uint16            mLastJobList;                           // index where the next job list to work on will be added
        csys::SysMutex    mAddJobMutex;
        uint32            mThreadNum;
        bool*             mJobsPrioritize;

        virtual int32 Run();
    };

    JobThread::JobThread(JobsThreadDescr descr)
        : mDescr(descr)
        , mFirstJobList(0)
        , mLastJobList(0)
        , mThreadNum(0)
    {
    }

    JobThread::~JobThread() {}

    void JobThread::Start(uint32 _threadNum)
    {
        mThreadNum = _threadNum;

        csys::SysWorkerThreadDescr threadDescr;
        threadDescr.Name = mDescr.Name;
        if (threadDescr.Name == nullptr)
            threadDescr.Name = "JobThread";
        threadDescr.Core      = mDescr.Core;
        threadDescr.StackSize = mDescr.StackSize;
        if (threadDescr.StackSize <= 0)
            threadDescr.StackSize = JOB_THREAD_STACK_SIZE;
        threadDescr.Priority = csys::PRIORITY_NORMAL;
        StartThread(threadDescr);
    }

    void JobThread::AddJobList(JobsListInstance* mJobsList)
    {
        // must lock because multiple mThreads may try to add new job lists at the same time
        mAddJobMutex.Lock();
        {
            // wait until there is space available because in rare cases multiple versions of the same job lists may still be queued
            while (mLastJobList - mFirstJobList >= CONFIG_MAX_JOBLISTS)
            {
                csys::SysYield();
            }
            DEBUG_ASSERT(mLastJobList - mFirstJobList < CONFIG_MAX_JOBLISTS);
            mJobListInstances[mLastJobList & (CONFIG_MAX_JOBLISTS - 1)] = mJobsList;
            mJobListVersions[mLastJobList & (CONFIG_MAX_JOBLISTS - 1)]  = mJobsList->GetVersion();
            mLastJobList++;
        }
        mAddJobMutex.Unlock();
    }

    int32 JobThread::Run()
    {
        JobsListState_t threadJobListState[CONFIG_MAX_JOBLISTS];
        int32           numJobLists        = 0;
        int32           lastStalledJobList = -1;

        while (!IsTerminating())
        {
            // fetch any new job lists and add them to the local list
            if (numJobLists < CONFIG_MAX_JOBLISTS && mFirstJobList < mLastJobList)
            {
                threadJobListState[numJobLists].mJobsList     = mJobListInstances[mFirstJobList & (CONFIG_MAX_JOBLISTS - 1)];
                threadJobListState[numJobLists].mVersion      = mJobListVersions[mFirstJobList & (CONFIG_MAX_JOBLISTS - 1)];
                threadJobListState[numJobLists].mSignalIndex  = 0;
                threadJobListState[numJobLists].mLastJobIndex = 0;
                threadJobListState[numJobLists].mNextJobIndex = -1;
                numJobLists++;
                mFirstJobList++;
            }
            if (numJobLists == 0)
            {
                break;
            }

            int32               currentJobList = 0;
            EJobsListPriority_t priority       = JOBSLIST_PRIORITY_NONE;
            if (lastStalledJobList < 0)
            {
                // find the job list with the highest priority
                for (int32 i = 0; i < numJobLists; i++)
                {
                    if (threadJobListState[i].mJobsList->GetPriority() > priority && !threadJobListState[i].mJobsList->WaitForOtherJobsList())
                    {
                        priority       = threadJobListState[i].mJobsList->GetPriority();
                        currentJobList = i;
                    }
                }
            }
            else
            {
                // try to hide the stall with a job from a list that has equal or higher priority
                currentJobList = lastStalledJobList;
                priority       = threadJobListState[lastStalledJobList].mJobsList->GetPriority();
                for (int32 i = 0; i < numJobLists; i++)
                {
                    if (i != lastStalledJobList && threadJobListState[i].mJobsList->GetPriority() >= priority && !threadJobListState[i].mJobsList->WaitForOtherJobsList())
                    {
                        priority       = threadJobListState[i].mJobsList->GetPriority();
                        currentJobList = i;
                    }
                }
            }

            // if the priority is high then try to run through the whole list to reduce the overhead
            // otherwise run a single job and re-evaluate priorities for the next job
            const bool singleJob = (priority == JOBSLIST_PRIORITY_HIGH) ? false : ((mJobsPrioritize != nullptr) ? *mJobsPrioritize : false);

            // try running one or more jobs from the current job list
            const int32 result = threadJobListState[currentJobList].mJobsList->RunJobs(mThreadNum, threadJobListState[currentJobList], singleJob);

            if ((result & JobsListInstance::RUN_DONE) != 0)
            {
                // done with this job list so remove it from the local list
                for (int32 i = currentJobList; i < numJobLists - 1; i++)
                {
                    threadJobListState[i] = threadJobListState[i + 1];
                }
                numJobLists--;
                lastStalledJobList = -1;
            }
            else if ((result & JobsListInstance::RUN_STALLED) != 0)
            {
                // yield when stalled on the same job list again without making any progress
                if (currentJobList == lastStalledJobList)
                {
                    if ((result & JobsListInstance::RUN_PROGRESS) == 0)
                    {
                        csys::SysYield();
                    }
                }
                lastStalledJobList = currentJobList;
            }
            else
            {
                lastStalledJobList = -1;
            }
        }
        return 0;
    }

    class JobsManagerLocal : public JobsManager
    {
    public:
        JobsManagerLocal(Alloc* allocator);
        virtual ~JobsManagerLocal() {}

        virtual void Init(JobsThreadDescr threads[], int32 num);
        virtual void Shutdown();

        virtual JobsList* AllocJobList(JobsListDescr const& descr);
        virtual void      FreeJobList(JobsList* mJobsList);

        virtual int32     GetNumJobLists() const;
        virtual int32     GetNumFreeJobLists() const;
        virtual JobsList* GetJobList(int32 index);

        virtual int32 GetNumProcessingUnits() const;

        virtual void WaitForAllJobLists();

        virtual bool        IsRegisteredJob(JobRun_t function) const;
        virtual void        RegisterJob(JobRun_t function, const char* name);
        virtual const char* GetJobName(JobRun_t function) const;

        void Submit(JobsListInstance* mJobsList, int32 parallelism);

        Alloc*           mAllocator;
        JobThread*       mThreads[CONFIG_MAX_JOBTHREADS];
        uint32           mMaxThreads;
        int32            mNumPhysicalCpuCores;
        int32            mNumLogicalCpuCores;
        int32            mNumCpuPackages;
        List<JobsList*>  mJobLists;
        JobsRegister     mJobsRegister;
    };

    JobsManagerLocal::JobsManagerLocal(Alloc* allocator)
        : mAllocator(allocator)
    {
    }

    JobsManager* CreateJobManager(Alloc* allocator)
    {
        JobsManagerLocal* manager = Construct<JobsManagerLocal>(allocator, TAG_JOBMANAGER, allocator);
        return manager;
    }

    void DestroyJobManager(JobsManager* manager)
    {
        JobsManagerLocal* localManager = static_cast<JobsManagerLocal*>(manager);
        Destruct(localManager->mAllocator, localManager);
    }

    void JobsManagerLocal::Init(JobsThreadDescr threads[], int32 num)
    {
        mJobLists.Init(mAllocator);
        mJobLists.SetCapacity(CONFIG_MAX_JOBLISTS);

        for (int32 i = 0; i < num; i++)
        {
            mThreads[i] = Construct<JobThread>(mAllocator, TAG_JOBTHREAD, threads[i]);
            mThreads[i]->Start(i);
        }
        mMaxThreads = num;
    }

    void JobsManagerLocal::Shutdown()
    {
        for (int32 i = 0; i < CONFIG_MAX_JOBTHREADS; i++)
        {
            mThreads[i]->StopThread();
            Destruct(mAllocator, mThreads[i]);
        }
        mJobLists.SetCapacity(0);
    }

    JobsList* JobsManagerLocal::AllocJobList(JobsListDescr const& descr)
    {
        JobsListId_t id      = mJobLists.Num();
        JobsList*    jobList = Construct<JobsList>(mAllocator, TAG_JOBLIST, mAllocator, id, descr);
        mJobLists.Append(jobList);
        return jobList;
    }

    void JobsManagerLocal::FreeJobList(JobsList* jobList)
    {
        if (jobList == nullptr)
        {
            return;
        }

        // wait for all job threads to finish because job list deletion is not thread safe
        for (uint32 i = 0; i < mMaxThreads; i++)
        {
            mThreads[i]->WaitForThread();
        }

        int32 index = mJobLists.FindIndex(jobList);
        DEBUG_ASSERT(index >= 0 && mJobLists[index] == jobList);
        mJobLists[index]->Wait();
        Destruct<JobsList>(mAllocator, jobList);
        mJobLists.RemoveIndexFast(index);
    }

    int32     JobsManagerLocal::GetNumJobLists() const { return mJobLists.Num(); }
    int32     JobsManagerLocal::GetNumFreeJobLists() const { return CONFIG_MAX_JOBLISTS - mJobLists.Num(); }
    JobsList* JobsManagerLocal::GetJobList(int32 index) { return mJobLists[index]; }
    int32     JobsManagerLocal::GetNumProcessingUnits() const { return mMaxThreads; }

    void JobsManagerLocal::WaitForAllJobLists()
    {
        for (int32 i = 0; i < mJobLists.Num(); i++)
        {
            mJobLists[i]->Wait();
        }
    }

    void JobsManagerLocal::Submit(JobsListInstance* mJobsList, int32 parallelism)
    {
        if (parallelism > CONFIG_MAX_JOBTHREADS)
            parallelism = CONFIG_MAX_JOBTHREADS;

        int32 numThreads = mMaxThreads;
        switch (parallelism)
        {
            case JOBSLIST_PARALLELISM_DEFAULT: numThreads = mMaxThreads; break;
            case JOBSLIST_PARALLELISM_MAX_CORES: numThreads = mNumLogicalCpuCores; break;
            case JOBSLIST_PARALLELISM_MAX_THREADS: numThreads = CONFIG_MAX_JOBTHREADS; break;
            default: numThreads = parallelism; break;
        }

        if (numThreads > 0)
        {
            for (int32 i = 0; i < numThreads; i++)
            {
                mThreads[i]->AddJobList(mJobsList);
                mThreads[i]->SignalWork();
            }
        }
        else
        {
            JobsListState_t state(mJobsList->GetVersion());
            mJobsList->RunJobs(0, state, false);
        }
    }

    bool        JobsManagerLocal::IsRegisteredJob(JobRun_t function) const { return mJobsRegister.IsRegisteredJob(function); }
    void        JobsManagerLocal::RegisterJob(JobRun_t function, const char* name) { mJobsRegister.RegisterJob(function, name); }
    const char* JobsManagerLocal::GetJobName(JobRun_t function) const { return mJobsRegister.GetJobNameByFunction(function); }

    int32 JobsRegister::Count() const { return mNumRegisteredJobs; }

    void JobsRegister::RegisterJob(JobRun_t function, const char* name)
    {
        if (IsRegisteredJob(function))
            return;
        mRegisteredJobFuncs[mNumRegisteredJobs] = function;
        mRegisteredJobNames[mNumRegisteredJobs] = name;
        mNumRegisteredJobs++;
    }

    bool JobsRegister::IsRegisteredJob(JobRun_t function) const
    {
        for (int32 i = 0; i < mNumRegisteredJobs; i++)
        {
            if (mRegisteredJobFuncs[i] == function)
            {
                return true;
            }
        }
        return false;
    }

    const char* JobsRegister::GetJobNameByIndex(int index) const
    {
        if (index >= 0 && index < mNumRegisteredJobs)
            return mRegisteredJobNames[index];
        return "unknown";
    }

    const char* JobsRegister::GetJobNameByFunction(JobRun_t function) const
    {
        for (int32 i = 0; i < mNumRegisteredJobs; i++)
        {
            if (mRegisteredJobFuncs[i] == function)
            {
                return mRegisteredJobNames[i];
            }
        }
        return "unknown";
    }

    uint32 ThreadStats_t::GetNumExecutedJobs() const { return mNumExecutedJobs; }
    uint32 ThreadStats_t::GetNumSyncs() const { return mNumExecutedSyncs; }
    uint64 ThreadStats_t::GetSubmitTimeMicroSec() const { return mSubmitTime; }
    uint64 ThreadStats_t::GetStartTimeMicroSec() const { return mStartTime; }
    uint64 ThreadStats_t::GetFinishTimeMicroSec() const { return mEndTime; }
    uint64 ThreadStats_t::GetWaitTimeMicroSec() const { return mWaitTime; }
    uint64 ThreadStats_t::GetTotalProcessingTimeMicroSec() const
    {
        uint64 total = 0;
        for (int32 unit = 0; unit < CONFIG_MAX_THREADS; unit++)
        {
            total += mThreadExecTime[unit];
        }
        return total;
    }
    uint64 ThreadStats_t::GetTotalWastedTimeMicroSec() const
    {
        uint64 total = 0;
        for (int32 unit = 0; unit < CONFIG_MAX_THREADS; unit++)
        {
            total += mThreadTotalTime[unit] - mThreadExecTime[unit];
        }
        return total;
    }
    uint64 ThreadStats_t::GetUnitProcessingTimeMicroSec(int32 unit) const
    {
        if (unit < 0 || unit >= CONFIG_MAX_THREADS)
        {
            return 0;
        }
        return mThreadExecTime[unit];
    }
    uint64 ThreadStats_t::GetUnitWastedTimeMicroSec(int32 unit) const
    {
        if (unit < 0 || unit >= CONFIG_MAX_THREADS)
        {
            return 0;
        }
        return mThreadTotalTime[unit] - mThreadExecTime[unit];
    }

} // namespace cjobs