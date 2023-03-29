#include "cjobs/c_jobs.h"
#include "cjobs/private/c_threading.h"

#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace cjobs
{
    struct InterlockedInteger
    {
        int32 GetValue();
        void  SetValue(int32 value);
        int32 Increment();
        int32 Decrement();
    };

    template <typename T> class List
    {
    public:
        List()
            : mAlloc(nullptr)
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

        T&    Alloc();
        void  Append(const T& item);
        int32 FindIndex(const T& item) const;
        void  RemoveIndexFast(int32 index);

        T& operator[](int32 index) { return mData[index]; }
        T  operator[](int32 index) const { return mData[index]; }

    protected:
        T*    mData;
        int32 mNum;
        int32 mCapacity;
    };

    uint64 g_Microseconds();

    void g_Printf(const char* format, ...);
    void g_Error(const char* format, ...);
    bool g_AssertFailed(const char* file, int line, const char* expression);

#define DEBUG_ASSERT(x)                         \
    if (!(x))                                   \
    {                                           \
        g_AssertFailed(__FILE__, __LINE__, #x); \
    }

#define DEBUG_VERIFY(x) ((x) ? true : (g_AssertFailed(__FILE__, __LINE__, #x), false))

    struct JobListState_t
    {
        JobListState_t()
            : mJobList(nullptr)
            , mVersion(0xFFFFFFFF)
            , mSignalIndex(0)
            , mLastJobIndex(0)
            , mNextJobIndex(-1)
        {
        }
        JobListState_t(int32 _version)
            : mJobList(nullptr)
            , mVersion(_version)
            , mSignalIndex(0)
            , mLastJobIndex(0)
            , mNextJobIndex(-1)
        {
        }
        JobListInstance* mJobList;
        int32            mVersion;
        int32            mSignalIndex;
        int32            mLastJobIndex;
        int32            mNextJobIndex;
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

    void JobsRegister::RegisterJob(JobRun_t function, const char* name)
    {
        if (IsRegisteredJob(function))
            return;
        mRegisteredJobFuncs[mNumRegisteredJobs] = function;
        mRegisteredJobNames[mNumRegisteredJobs] = name;
        mNumRegisteredJobs++;
    }

    class JobListRegister
    {
    public:
        JobListRegister();
        ~JobListRegister();

        JobListId_t RegisterJobList(JobList* mJobList);
        void        UnregisterJobList(JobList* mJobList);

        JobListInstance* FindJobList(JobListId_t id) const;
        const char*      FindJobListName(JobListId_t id) const;

    protected:
        JobListInstance* mRegisteredJobLists[CONFIG_MAX_REGISTERED_JOBS];
        const char*      mRegisteredJobListNames[CONFIG_MAX_REGISTERED_JOBS];
    };

    class JobListInstance
    {
    public:
        JobListInstance(Alloc* allocator, JobListId_t id, const char* name, EJobListPriority_t priority, uint32 mMaxJobs, uint32 mMaxSyncs);
        ~JobListInstance();

        // These are called from the one thread that manages this list.
        inline void AddJob(JobRun_t function, void* data);
        inline void InsertSyncPoint(EJobSyncType_t syncType);
        void        Submit(JobListInstance* waitForJobList_, int32 parallelism);
        void        Wait();
        bool        TryWait();
        bool        IsSubmitted() const;

        JobListId_t          GetId() const { return mListId; }
        EJobListPriority_t   GetPriority() const { return mListPriority; }
        int32                GetVersion() { return mVersion.GetValue(); }
        ThreadStats_t*       GetThreadStats() { return &mThreadStats; }
        ThreadStats_t const* GetThreadStats() const { return &mThreadStats; }

        bool WaitForOtherJobList();

        // This is thread safe and called from the job mThreads.
        enum runResult_t
        {
            RUN_OK       = 0,
            RUN_PROGRESS = 1 << 0,
            RUN_DONE     = 1 << 1,
            RUN_STALLED  = 1 << 2
        };

        int32 RunJobs(uint32 mThreadNum, JobListState_t& state, bool singleJob);

        static const int32 NUM_DONE_GUARDS = 4; // cycle through 4 guards so we can cyclicly chain job lists

        bool                mThreaded;
        bool                mDone;
        bool                mHasSignal;
        JobListId_t         mListId;
        const char*         mListName;
        EJobListPriority_t  mListPriority;
        uint32              mMaxJobs;
        uint32              mMaxSyncs;
        uint32              mNumSyncs;
        int32               mLastSignalJob;
        InterlockedInteger* mWaitForGuard;
        InterlockedInteger  mDoneGuards[NUM_DONE_GUARDS];
        int32               mCurrentDoneGuard;
        InterlockedInteger  mVersion;
        struct job_t
        {
            JobRun_t function;
            void*    data;
            int32    executed;
        };
        int32                          mJobListIndexDebug;
        List<job_t>              mJobList;
        List<InterlockedInteger> mSignalJobCount;
        InterlockedInteger             mCurrentJob;
        InterlockedInteger             mFetchLock;
        InterlockedInteger             mNumThreadsExecuting;

        JobsRegister* mJobsRegister;

        ThreadStats_t mDeferredThreadStats;
        ThreadStats_t mThreadStats;

        int32 RunJobsInternal(uint32 mThreadNum, JobListState_t& state, bool singleJob);

        static void Nop(void* data) {}

        static int32 JOB_SIGNAL;
        static int32 JOB_SYNCHRONIZE;
        static int32 JOB_LIST_DONE;
    };

    int32 JobListInstance::JOB_SIGNAL;
    int32 JobListInstance::JOB_SYNCHRONIZE;
    int32 JobListInstance::JOB_LIST_DONE;

    JobListInstance::JobListInstance(Alloc* allocator, JobListId_t id, const char* name, EJobListPriority_t priority, uint32 mMaxJobs, uint32 mMaxSyncs)
        : mThreaded(true)
        , mDone(true)
        , mHasSignal(false)
        , mListId(id)
        , mListName(name)
        , mListPriority(priority)
        , mNumSyncs(0)
        , mLastSignalJob(0)
        , mWaitForGuard(nullptr)
        , mCurrentDoneGuard(0)
        , mJobList()
        , mSignalJobCount()
    {
        DEBUG_ASSERT(mListPriority != JOBLIST_PRIORITY_NONE);

        this->mMaxJobs  = mMaxJobs;
        this->mMaxSyncs = mMaxSyncs;
        mJobList.Init(allocator);
        mJobList.SetCapacity(mMaxJobs + mMaxSyncs * 2 + 1); // syncs go in as dummy jobs and one more to update the doneCount
        mJobList.SetNum(0);
        mSignalJobCount.Init(allocator);
        mSignalJobCount.SetCapacity(mMaxSyncs + 1); // need one extra for submit
        mSignalJobCount.SetNum(0);

        memset(&mDeferredThreadStats, 0, sizeof(ThreadStats_t));
        memset(&mThreadStats, 0, sizeof(ThreadStats_t));
    }

    JobListInstance::~JobListInstance()
    {
        Wait();
        mJobList.SetCapacity(0);
        mSignalJobCount.SetCapacity(0);
    }

    inline void JobListInstance::AddJob(JobRun_t function, void* data)
    {
        DEBUG_ASSERT(mDone);

#if defined(PLATFORM_DEBUG)
        // don't check all jobs each time we come here, just check a part and next time another part
        for (int32 i = 0; i < 10; i++)
        {
            mJobListIndexDebug = (mJobListIndexDebug + 1);
            if (mJobListIndexDebug >= mJobList.Num())
                mJobListIndexDebug = 0;
            DEBUG_ASSERT(mJobList[mJobListIndexDebug].function != function || mJobList[mJobListIndexDebug].data != data);
        }
#endif
        if (!mJobList.IsFull())
        {
            job_t& job   = mJobList.Alloc();
            job.function = function;
            job.data     = data;
            job.executed = 0;
        }
        else
        {
            // debug output to show us what is overflowing
            int32 currentJobCount[CONFIG_MAX_REGISTERED_JOBS] = {};

            for (int32 i = 0; i < mJobList.Num(); ++i)
            {
                const char* jobName = mJobsRegister->GetJobNameByFunction(mJobList[i].function);
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
            g_Error("Can't add job '%s', too many jobs %d", mJobsRegister->GetJobNameByFunction(function), mJobList.Num());
        }
    }

    inline void JobListInstance::InsertSyncPoint(EJobSyncType_t syncType)
    {
        DEBUG_ASSERT(mDone);
        switch (syncType)
        {
            case JOB_SYNC_SIGNAL:
            {
                DEBUG_ASSERT(!mHasSignal);
                if (mJobList.Num())
                {
                    DEBUG_ASSERT(!mHasSignal);
                    mSignalJobCount.Alloc();
                    mSignalJobCount[mSignalJobCount.Num() - 1].SetValue(mJobList.Num() - mLastSignalJob);
                    mLastSignalJob = mJobList.Num();
                    job_t& job     = mJobList.Alloc();
                    job.function   = Nop;
                    job.data       = &JOB_SIGNAL;
                    mHasSignal     = true;
                }
                break;
            }
            case JOB_SYNC_SYNCHRONIZE:
            {
                if (mHasSignal)
                {
                    job_t& job   = mJobList.Alloc();
                    job.function = Nop;
                    job.data     = &JOB_SYNCHRONIZE;
                    mHasSignal   = false;
                    mNumSyncs++;
                }
                break;
            }
        }
    }

    void JobListInstance::Submit(JobListInstance* waitForJobList, int32 parallelism)
    {
        DEBUG_ASSERT(mDone);
        DEBUG_ASSERT(mNumSyncs <= mMaxSyncs);
        DEBUG_ASSERT((uint32)mJobList.Num() <= mMaxJobs + mNumSyncs * 2);
        DEBUG_ASSERT(mFetchLock.GetValue() == 0);

        mDone = false;
        mCurrentJob.SetValue(0);

        memset(&mDeferredThreadStats, 0, sizeof(mDeferredThreadStats));
        mDeferredThreadStats.mNumExecutedJobs  = mJobList.Num() - mNumSyncs * 2;
        mDeferredThreadStats.mNumExecutedSyncs = mNumSyncs;
        mDeferredThreadStats.mSubmitTime       = g_Microseconds();
        mDeferredThreadStats.mStartTime        = 0;
        mDeferredThreadStats.mEndTime          = 0;
        mDeferredThreadStats.mWaitTime         = 0;

        if (mJobList.Num() == 0)
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

        mSignalJobCount.Alloc();
        mSignalJobCount[mSignalJobCount.Num() - 1].SetValue(mJobList.Num() - mLastSignalJob);

        job_t& job   = mJobList.Alloc();
        job.function = Nop;
        job.data     = &JOB_LIST_DONE;

        if (mThreaded)
        {
            // hand over to the manager
            void SubmitJobList(JobListInstance * mJobList, int32 parallelism);
            SubmitJobList(this, parallelism);
        }
        else
        {
            // run all the jobs right here
            JobListState_t state(GetVersion());
            RunJobs(0, state, false);
        }
    }

    void JobListInstance::Wait()
    {
        if (mJobList.Num() > 0)
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
                cthread::Yield();
                waited = true;
            }
            mVersion.Increment();
            while (mNumThreadsExecuting.GetValue() > 0)
            {
                cthread::Yield();
                waited = true;
            }

            mJobList.SetNum(0);
            mSignalJobCount.SetNum(0);
            mNumSyncs      = 0;
            mLastSignalJob = 0;

            uint64 waitEnd                 = g_Microseconds();
            mDeferredThreadStats.mWaitTime = waited ? (waitEnd - waitStart) : 0;
        }
        memcpy(&mThreadStats, &mDeferredThreadStats, sizeof(mThreadStats));
        mDone = true;
    }

    bool JobListInstance::TryWait()
    {
        if (mJobList.Num() == 0 || mSignalJobCount[mSignalJobCount.Num() - 1].GetValue() <= 0)
        {
            Wait();
            return true;
        }
        return false;
    }

    bool JobListInstance::IsSubmitted() const { return !mDone; }

#ifdef PLATFORM_DEBUG
    volatile float    longJobTime;
    volatile JobRun_t longJobFunc;
    volatile void*    longJobData;
#endif

    int32 JobListInstance::RunJobsInternal(uint32 mThreadNum, JobListState_t& state, bool singleJob)
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
            for (; state.mLastJobIndex < (int32)mCurrentJob.GetValue() && state.mLastJobIndex < mJobList.Num(); state.mLastJobIndex++)
            {
                if (mJobList[state.mLastJobIndex].data == &JOB_SIGNAL)
                {
                    state.mSignalIndex++;
                    DEBUG_ASSERT(state.mSignalIndex < mSignalJobCount.Num());
                }
                else if (mJobList[state.mLastJobIndex].data == &JOB_SYNCHRONIZE)
                {
                    DEBUG_ASSERT(state.mSignalIndex > 0);
                    if (mSignalJobCount[state.mSignalIndex - 1].GetValue() > 0)
                    {
                        // stalled on a synchronization point
                        return (result | RUN_STALLED);
                    }
                }
                else if (mJobList[state.mLastJobIndex].data == &JOB_LIST_DONE)
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
                for (; state.mLastJobIndex <= state.mNextJobIndex && state.mLastJobIndex < mJobList.Num(); state.mLastJobIndex++)
                {
                    if (mJobList[state.mLastJobIndex].data == &JOB_SIGNAL)
                    {
                        state.mSignalIndex++;
                        DEBUG_ASSERT(state.mSignalIndex < mSignalJobCount.Num());
                    }
                    else if (mJobList[state.mLastJobIndex].data == &JOB_SYNCHRONIZE)
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
                    else if (mJobList[state.mLastJobIndex].data == &JOB_LIST_DONE)
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
            if (state.mNextJobIndex >= mJobList.Num())
            {
                return (result | RUN_DONE);
            }

            // execute the next job
            {
                uint64 jobStart = g_Microseconds();

                mJobList[state.mNextJobIndex].function(mJobList[state.mNextJobIndex].data);
                mJobList[state.mNextJobIndex].executed = 1;

                uint64 jobEnd = g_Microseconds();
                mDeferredThreadStats.mThreadExecTime[mThreadNum] += jobEnd - jobStart;

#ifdef PLATFORM_DEBUG
                if (jobs_longJobMicroSec.GetInteger() > 0)
                {
                    if (jobEnd - jobStart > jobs_longJobMicroSec.GetInteger() && GetId() != JOBLIST_UTILITY)
                    {
                        longJobTime             = (jobEnd - jobStart) * (1.0f / 1000.0f);
                        longJobFunc             = mJobList[state.mNextJobIndex].function;
                        longJobData             = mJobList[state.mNextJobIndex].data;
                        const char* jobName     = GetJobName(mJobList[state.mNextJobIndex].function);
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

    int32 JobListInstance::RunJobs(uint32 mThreadNum, JobListState_t& state, bool singleJob)
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

    bool JobListInstance::WaitForOtherJobList()
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

    JobList::JobList(Alloc* allocator, JobListId_t id, const char* name, EJobListPriority_t priority, uint32 mMaxJobs, uint32 mMaxSyncs, const uint32 color)
        : mAllocator(allocator)
        , mColor(color)
    {
        DEBUG_ASSERT(priority > JOBLIST_PRIORITY_NONE);
        this->mJobListInstance = mAllocator->Construct<JobListInstance>(TAG_JOBLIST, allocator, id, name, priority, mMaxJobs, mMaxSyncs);
    }

    JobList::~JobList() { mAllocator->Destruct(mJobListInstance); }

    void JobList::AddJob(JobRun_t function, void* data)
    {
        DEBUG_ASSERT(this->mJobListInstance->mJobsRegister->IsRegisteredJob(function));
        mJobListInstance->AddJob(function, data);
    }

    void JobList::InsertSyncPoint(EJobSyncType_t syncType) { mJobListInstance->InsertSyncPoint(syncType); }

    void JobList::Wait()
    {
        if (mJobListInstance != nullptr)
        {
            mJobListInstance->Wait();
        }
    }

    bool JobList::TryWait()
    {
        bool mDone = true;
        if (mJobListInstance != nullptr)
        {
            mDone &= mJobListInstance->TryWait();
        }
        return mDone;
    }

    void JobList::Submit(JobList* waitForJobList, int32 parallelism)
    {
        DEBUG_ASSERT(waitForJobList != this);
        mJobListInstance->Submit((waitForJobList != nullptr) ? waitForJobList->mJobListInstance : nullptr, parallelism);
    }

    bool                 JobList::IsSubmitted() const { return mJobListInstance->IsSubmitted(); }
    JobListId_t          JobList::GetId() const { return mJobListInstance->GetId(); }
    ThreadStats_t const* JobList::GetStats() const { return mJobListInstance->GetThreadStats(); }

    const int32 JOB_THREAD_STACK_SIZE = 256 * 1024; // same size as the SPU local store

    class JobThread : public cthread::Thread
    {
    public:
        JobThread();
        ~JobThread();

        void Start(cthread::core_t _core, uint32 _threadNum);
        void AddJobList(JobListInstance* mJobList);

    protected:
        JobListInstance* mJobListInstances[CONFIG_MAX_JOBLISTS]; // cyclic buffer with job lists
        int32            mJobListVersions[CONFIG_MAX_JOBLISTS];  // cyclic buffer with job lists
        uint32           mFirstJobList;                          // index of the last job list the thread grabbed
        uint32           mLastJobList;                           // index where the next job list to work on will be added
        cthread::Mutex   mAddJobMutex;
        char             mName[64];
        uint32           mThreadNum;
        bool*            mJobsPrioritize;

        virtual int32 Run();
    };

    JobThread::JobThread()
        : mFirstJobList(0)
        , mLastJobList(0)
        , mThreadNum(0)
    {
    }

    JobThread::~JobThread() {}

    void JobThread::Start(cthread::core_t _core, uint32 _threadNum)
    {
        this->mThreadNum = _threadNum;
        strcpy(mName, "JobListProcessor_00");
        itoa(_threadNum, mName + strlen(mName), 10);
        StartWorkerThread(mName, _core, cthread::PRIORITY_NORMAL, JOB_THREAD_STACK_SIZE);
    }

    void JobThread::AddJobList(JobListInstance* mJobList)
    {
        // must lock because multiple mThreads may try to add new job lists at the same time
        mAddJobMutex.Lock();
        {
            // wait until there is space available because in rare cases multiple versions of the same job lists may still be queued
            while (mLastJobList - mFirstJobList >= CONFIG_MAX_JOBLISTS)
            {
                cthread::Yield();
            }
            DEBUG_ASSERT(mLastJobList - mFirstJobList < CONFIG_MAX_JOBLISTS);
            mJobListInstances[mLastJobList & (CONFIG_MAX_JOBLISTS - 1)] = mJobList;
            mJobListVersions[mLastJobList & (CONFIG_MAX_JOBLISTS - 1)] = mJobList->GetVersion();
            mLastJobList++;
        }
        mAddJobMutex.Unlock();
    }

    int32 JobThread::Run()
    {
        JobListState_t threadJobListState[CONFIG_MAX_JOBLISTS];
        int32          numJobLists        = 0;
        int32          lastStalledJobList = -1;

        while (!IsTerminating())
        {
            // fetch any new job lists and add them to the local list
            if (numJobLists < CONFIG_MAX_JOBLISTS && mFirstJobList < mLastJobList)
            {
                threadJobListState[numJobLists].mJobList      = mJobListInstances[mFirstJobList & (CONFIG_MAX_JOBLISTS - 1)];
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

            int32              currentJobList = 0;
            EJobListPriority_t priority       = JOBLIST_PRIORITY_NONE;
            if (lastStalledJobList < 0)
            {
                // find the job list with the highest priority
                for (int32 i = 0; i < numJobLists; i++)
                {
                    if (threadJobListState[i].mJobList->GetPriority() > priority && !threadJobListState[i].mJobList->WaitForOtherJobList())
                    {
                        priority       = threadJobListState[i].mJobList->GetPriority();
                        currentJobList = i;
                    }
                }
            }
            else
            {
                // try to hide the stall with a job from a list that has equal or higher priority
                currentJobList = lastStalledJobList;
                priority       = threadJobListState[lastStalledJobList].mJobList->GetPriority();
                for (int32 i = 0; i < numJobLists; i++)
                {
                    if (i != lastStalledJobList && threadJobListState[i].mJobList->GetPriority() >= priority && !threadJobListState[i].mJobList->WaitForOtherJobList())
                    {
                        priority       = threadJobListState[i].mJobList->GetPriority();
                        currentJobList = i;
                    }
                }
            }

            // if the priority is high then try to run through the whole list to reduce the overhead
            // otherwise run a single job and re-evaluate priorities for the next job
            bool singleJob = (priority == JOBLIST_PRIORITY_HIGH) ? false : ((mJobsPrioritize != nullptr) ? *mJobsPrioritize : false);

            // try running one or more jobs from the current job list
            int32 result = threadJobListState[currentJobList].mJobList->RunJobs(mThreadNum, threadJobListState[currentJobList], singleJob);

            if ((result & JobListInstance::RUN_DONE) != 0)
            {
                // done with this job list so remove it from the local list
                for (int32 i = currentJobList; i < numJobLists - 1; i++)
                {
                    threadJobListState[i] = threadJobListState[i + 1];
                }
                numJobLists--;
                lastStalledJobList = -1;
            }
            else if ((result & JobListInstance::RUN_STALLED) != 0)
            {
                // yield when stalled on the same job list again without making any progress
                if (currentJobList == lastStalledJobList)
                {
                    if ((result & JobListInstance::RUN_PROGRESS) == 0)
                    {
                        cthread::Yield();
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
        virtual ~JobsManagerLocal() {}

        virtual void Init(Alloc* allocator, int32 jobs_numThreads);
        virtual void Shutdown();

        virtual JobList* AllocJobList(const char* name, EJobListPriority_t priority, uint32 mMaxJobs, uint32 mMaxSyncs, const uint32 color);
        virtual void     FreeJobList(JobList* mJobList);

        virtual int32    GetNumJobLists() const;
        virtual int32    GetNumFreeJobLists() const;
        virtual JobList* GetJobList(int32 index);

        virtual int32 GetNumProcessingUnits();

        virtual void WaitForAllJobLists();

        virtual bool        IsRegisteredJob(JobRun_t function) const;
        virtual void        RegisterJob(JobRun_t function, const char* name);
        virtual const char* GetJobName(JobRun_t function) const;

        void Submit(JobListInstance* mJobList, int32 parallelism);

    private:
        Alloc*               mAllocator;
        JobThread            mThreads[CONFIG_MAX_JOBTHREADS];
        uint32               mMaxThreads;
        int32                mNumPhysicalCpuCores;
        int32                mNumLogicalCpuCores;
        int32                mNumCpuPackages;
        List<JobList*>       mJobLists;
        JobListRegister      mJobListRegister;
        JobsRegister         mJobsRegister;
    };

    JobsManagerLocal g_JobManagerLocal;
    JobsManager*     g_JobManager = &g_JobManagerLocal;

    void SubmitJobList(JobListInstance* mJobList, int32 parallelism) { g_JobManagerLocal.Submit(mJobList, parallelism); }

    void JobsManagerLocal::Init(Alloc* allocator, int32 jobs_numThreads)
    {
        mAllocator = allocator;
        mJobLists.Init(allocator);
        mJobLists.SetCapacity(CONFIG_MAX_JOBLISTS);

        cthread::CPUCount(mNumPhysicalCpuCores, mNumLogicalCpuCores, mNumCpuPackages);
        for (int32 i = 0; i < CONFIG_MAX_JOBTHREADS; i++)
        {
            mThreads[i].Start(cthread::ThreadToCore(i), i);
        }
        mMaxThreads = jobs_numThreads;
    }

    void JobsManagerLocal::Shutdown()
    {
        for (int32 i = 0; i < CONFIG_MAX_JOBTHREADS; i++)
        {
            mThreads[i].StopThread();
        }
        mJobLists.SetCapacity(0);
    }

    JobList* JobsManagerLocal::AllocJobList(const char* name, EJobListPriority_t priority, uint32 mMaxJobs, uint32 mMaxSyncs, const uint32 color)
    {
        for (int32 i = 0; i < mJobLists.Num(); i++)
        {
            if (mJobLists[i]->GetName() == name)
            {
                // certain 'users' may cause job lists to be allocated multiple times
            }
        }

        JobListId_t id      = mJobLists.Num();
        JobList*    jobList = mAllocator->Construct<JobList>(TAG_JOBLIST, mAllocator, id, name, priority, mMaxJobs, mMaxSyncs, color);
        mJobLists.Append(jobList);
        return jobList;
    }

    void JobsManagerLocal::FreeJobList(JobList* jobList)
    {
        if (jobList == nullptr)
        {
            return;
        }

        // wait for all job threads to finish because job list deletion is not thread safe
        for (uint32 i = 0; i < mMaxThreads; i++)
        {
            mThreads[i].WaitForThread();
        }

        int32 index = mJobLists.FindIndex(jobList);
        DEBUG_ASSERT(index >= 0 && mJobLists[index] == jobList);
        mJobLists[index]->Wait();
        mAllocator->Destruct<JobList>(jobList);
        mJobLists.RemoveIndexFast(index);
    }

    int32    JobsManagerLocal::GetNumJobLists() const { return mJobLists.Num(); }
    int32    JobsManagerLocal::GetNumFreeJobLists() const { return CONFIG_MAX_JOBLISTS - mJobLists.Num(); }
    JobList* JobsManagerLocal::GetJobList(int32 index) { return mJobLists[index]; }
    int32    JobsManagerLocal::GetNumProcessingUnits() { return mMaxThreads; }

    void JobsManagerLocal::WaitForAllJobLists()
    {
        for (int32 i = 0; i < mJobLists.Num(); i++)
        {
            mJobLists[i]->Wait();
        }
    }

    void JobsManagerLocal::Submit(JobListInstance* mJobList, int32 parallelism)
    {
        if (parallelism > CONFIG_MAX_JOBTHREADS)
            parallelism = CONFIG_MAX_JOBTHREADS;

        int32 numThreads = mMaxThreads;
        switch (parallelism)
        {
            case JOBLIST_PARALLELISM_DEFAULT: numThreads = mMaxThreads; break;
            case JOBLIST_PARALLELISM_MAX_CORES: numThreads = mNumLogicalCpuCores; break;
            case JOBLIST_PARALLELISM_MAX_THREADS: numThreads = CONFIG_MAX_JOBTHREADS; break;
            default: numThreads = parallelism; break;
        }

        if (numThreads > 0)
        {
            for (int32 i = 0; i < numThreads; i++)
            {
                mThreads[i].AddJobList(mJobList);
                mThreads[i].SignalWork();
            }
        }
        else
        {
            JobListState_t state(mJobList->GetVersion());
            mJobList->RunJobs(0, state, false);
        }
    }

    bool        JobsManagerLocal::IsRegisteredJob(JobRun_t function) const { return mJobsRegister.IsRegisteredJob(function); }
    void        JobsManagerLocal::RegisterJob(JobRun_t function, const char* name) { mJobsRegister.RegisterJob(function, name); }
    const char* JobsManagerLocal::GetJobName(JobRun_t function) const { return mJobsRegister.GetJobNameByFunction(function); }

    int32 JobsRegister::Count() const { return mNumRegisteredJobs; }

    void JobsRegister::RegisterJob(JobRun_t function, const char* name)
    {
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