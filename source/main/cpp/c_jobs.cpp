#include "cjobs/c_jobs.h"
#include "cjobs/private/c_sys.h"

#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace cjobs
{
    typedef csys::SysInterlockedInteger InterlockedInt;

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

    //-----------------------------------------------------------------------------
    template <typename T, typename... Args> inline T* Construct(Alloc* a, int32 tag, Args&&... args)
    {
        void* mem = a->Allocate(sizeof(T), sizeof(void*), tag);
        return new (mem) T(args...);
    }

    template <typename T> inline void Destruct(Alloc* a, T* ptr)
    {
        ptr->~T();
        a->Deallocate(ptr);
    }

    template <typename T> class Array
    {
    public:
        Array()
            : mData(nullptr)
            , mCount(0)
            , mCapacity(0)
        {
        }

        void SetCapacity(Alloc* alloc, int32 n);

        int32 Count() const { return mCount; }
        int32 Capacity() const { return mCapacity; }
        void  SetCount(int32 n) { mCount = n; }

        T&    Append();
        int32 Find(const T& item) const;
        void  Remove(int32 index);

        inline T& operator[](int32 index) { return mData[index]; }
        inline T  operator[](int32 index) const { return mData[index]; }

    protected:
        T*    mData;
        int32 mCount;
        int32 mCapacity;
    };

    template <typename T> void Array<T>::SetCapacity(Alloc* alloc, int32 n)
    {
        if (n == 0)
        {
            if (mData)
            {
                alloc->Deallocate(mData);
                mData = nullptr;
            }
            mCapacity = 0;
            mCount    = 0;
        }
        else if (n > mCapacity)
        {
            T* newData = (T*)alloc->Allocate(n * sizeof(T), sizeof(void*));
            if (mData)
            {
                memcpy(newData, mData, mCount * sizeof(T));
                alloc->Deallocate(mData);
            }
            mData     = newData;
            mCapacity = n;
        }
    }

    template <typename T> T& Array<T>::Append()
    {
        if (mCount == mCapacity)
        {
            return mData[mCount - 1];
        }
        return mData[mCount++];
    }

    template <typename T> int32 Array<T>::Find(const T& item) const
    {
        for (int32 i = 0; i < mCount; ++i)
        {
            if (mData[i] == item)
            {
                return i;
            }
        }
        return -1;
    }

    template <typename T> void Array<T>::Remove(int32 index)
    {
        DEBUG_ASSERT(index >= 0 && index < mCount);
        mData[index] = mData[mCount - 1];
        --mCount;
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

    class JobsListStatsInstance
    {
    public:
        uint32 mNumExecutedJobs;
        uint32 mNumExecutedSyncs;
        uint64 mSubmitTime;
        uint64 mStartTime;
        uint64 mEndTime;
        uint64 mWaitTime;
        uint64 mThreadExecTime[CONFIG_MAX_THREADS];
        uint64 mThreadTotalTime[CONFIG_MAX_THREADS];
    };

    class JobsListInstance
    {
    public:
        JobsListInstance(JobsManagerLocal* manager, JobsRegister* jobsRegister, Alloc * allocator, JobsListId_t id, JobsListDescr const& descr);
        ~JobsListInstance();

        // These are called from the one thread that manages this list.
        inline void AddJob(JobRun_t function, void* data);
        inline void InsertSyncPoint(EJobsSyncType_t syncType);
        void        Submit(JobsListInstance* waitForJobList_, int32 parallelism);
        void        Wait();
        bool        TryWait();
        bool        IsSubmitted() const;

        JobsListId_t                 GetId() const { return mListId; }
        JobsListDescr const&         GetDescr() const { return mDescr; }
        EJobsListPriority_t          GetPriority() const { return mDescr.Priority; }
        int32                        GetVersion() { return mVersion.GetValue(); }
        JobsListStatsInstance*       GetStats() { return &mStats; }
        JobsListStatsInstance const* GetStats() const { return &mStats; }

        bool WaitForOtherJobsList();

        // This is thread safe and called from the job threads.
        enum EWorkerState
        {
            STATE_OK       = 0,
            STATE_PROGRESS = 1 << 0,
            STATE_DONE     = 1 << 1,
            STATE_STALLED  = 1 << 2
        };
        int32 RunJobs(uint32 threadIndex, JobsListState_t& state, bool singleJob);

        void* operator new(uint64 num_bytes, void* mem) { return mem; }
        void  operator delete(void* mem, void*) {}
        void* operator new(uint64 num_bytes) noexcept { return nullptr; }
        void  operator delete(void* mem) {}

        static const int32 NUM_DONE_GUARDS = 4; // cycle through 4 guards so we can cyclicly chain job lists

        JobsManagerLocal* mManager;
        JobsRegister*     mJobsRegister;
        Alloc*            mAllocator;
        bool              mThreaded;
        bool              mDone;
        bool              mHasSignal;
        JobsListId_t      mListId;
        JobsListDescr     mDescr;
        int16             mNumSyncs;
        int32             mLastSignalJob;
        InterlockedInt*   mWaitForGuard;
        InterlockedInt    mDoneGuards[NUM_DONE_GUARDS];
        int32             mCurrentDoneGuard;
        InterlockedInt    mVersion;
        struct job_t
        {
            JobRun_t function;
            void*    data;
        };
        int32                 mJobListIndexDebug;
        Array<job_t>          mJobsList;
        Array<InterlockedInt> mSignalJobCount;
        InterlockedInt        mCurrentJob;
        InterlockedInt        mFetchLock;
        InterlockedInt        mNumThreadsExecuting;
        JobsListStatsInstance mDeferredStats;
        JobsListStatsInstance mStats;

        int32 RunJobsInternal(uint32 threadIndex, JobsListState_t& state, bool singleJob);

        static void Nop(void* data) {}

        static int32 JOB_SIGNAL;
        static int32 JOB_SYNCHRONIZE;
        static int32 JOB_LIST_DONE;
    };

    int32 JobsListInstance::JOB_SIGNAL;
    int32 JobsListInstance::JOB_SYNCHRONIZE;
    int32 JobsListInstance::JOB_LIST_DONE;

    class JobThread : public csys::SysWorkerThread
    {
    public:
        JobThread(JobsThreadDescr descr, bool* jobsPrioritize);
        ~JobThread();

        void Start(uint16 threadNum);
        void AddJobList(JobsListInstance* jobsList);

        void* operator new(uint64 num_bytes, void* mem) { return mem; }
        void  operator delete(void* mem, void*) {}
        void* operator new(uint64 num_bytes) noexcept { return nullptr; }
        void  operator delete(void* mem) {}

    protected:
        JobsThreadDescr   mDescr;
        csys::SysMutex    mAddJobMutex;
        JobsListInstance* mJobListInstances[CONFIG_MAX_JOBLISTS]; // cyclic buffer with job lists
        bool*             mJobsPrioritize;
        int32             mJobListVersions[CONFIG_MAX_JOBLISTS]; // cyclic buffer with job lists
        uint16            mFirstJobList;                         // index of the last job list the thread grabbed
        uint16            mLastJobList;                          // index where the next job list to work on will be added
        uint16            mThreadIndex;

        virtual int32 Run();
    };

    class JobsManagerLocal
    {
    public:
        JobsManagerLocal(Alloc* allocator);
        ~JobsManagerLocal() {}

        void Init(JobsThreadDescr threads[], int32 num);
        void Shutdown();

        JobsList AllocJobList(JobsListDescr const& descr);
        void     FreeJobList(JobsList& jobsList);

        int32    GetNumJobLists() const;
        int32    GetNumFreeJobLists() const;
        JobsList GetJobList(int32 index);

        int32 GetNumProcessingUnits() const;

        void WaitForAllJobLists();

        bool        IsRegisteredJob(JobRun_t function) const;
        void        RegisterJob(JobRun_t function, const char* name);
        const char* GetJobName(JobRun_t function) const;

        void Submit(JobsListInstance* jobsList, int32 parallelism);

        void* operator new(uint64 num_bytes, void* mem) { return mem; }
        void  operator delete(void* mem, void*) {}
        void* operator new(uint64 num_bytes) noexcept { return nullptr; }
        void  operator delete(void* mem) {}

        Alloc*                   mAllocator;
        JobThread*               mThreads[CONFIG_MAX_THREADS];
        bool                     mJobsPrioritize;
        uint32                   mMaxThreads;
        Array<JobsListInstance*> mJobLists;
        JobsRegister             mJobsRegister;
    };

    JobsListInstance::JobsListInstance(JobsManagerLocal* manager, JobsRegister* jobsRegister, Alloc* allocator, JobsListId_t id, JobsListDescr const& descr)
        : mManager(manager)
        , mJobsRegister(jobsRegister)
        , mAllocator(allocator)
        , mThreaded(true)
        , mDone(true)
        , mHasSignal(false)
        , mListId(id)
        , mDescr(descr)
        , mNumSyncs(0)
        , mLastSignalJob(0)
        , mJobListIndexDebug(0)
        , mWaitForGuard(nullptr)
        , mCurrentDoneGuard(0)
        , mJobsList()
        , mSignalJobCount()
    {
        DEBUG_ASSERT(mDescr.Priority != JOBSLIST_PRIORITY_NONE);

        mJobsList.SetCapacity(mAllocator, mDescr.MaxJobs + mDescr.MaxSyncs * 2 + 1); // syncs go in as dummy jobs and one more to update the doneCount
        mJobsList.SetCount(0);
        mSignalJobCount.SetCapacity(mAllocator, mDescr.MaxSyncs + 1); // need one extra for submit
        mSignalJobCount.SetCount(0);

        memset(&mDeferredStats, 0, sizeof(JobsListStats));
        memset(&mStats, 0, sizeof(JobsListStats));
    }

    JobsListInstance::~JobsListInstance()
    {
        Wait();
        mJobsList.SetCapacity(mAllocator, 0);
        mSignalJobCount.SetCapacity(mAllocator, 0);
    }

    inline void JobsListInstance::AddJob(JobRun_t function, void* data)
    {
        DEBUG_ASSERT(mDone);

#if defined(PLATFORM_DEBUG)
        // don't check all jobs each time we come here, just check a part and next time another part
        for (int32 i = 0; i < 10; i++)
        {
            mJobListIndexDebug = (mJobListIndexDebug + 1);
            if (mJobListIndexDebug >= mJobsList.Count())
                mJobListIndexDebug = 0;
            DEBUG_ASSERT(mJobsList[mJobListIndexDebug].function != function || mJobsList[mJobListIndexDebug].data != data);
        }
#endif
        if (mJobsList.Count() < mJobsList.Capacity())
        {
            job_t& job   = mJobsList.Append();
            job.function = function;
            job.data     = data;
        }
        else
        {
            // debug output to show us what is overflowing
            int32 currentJobCount[CONFIG_MAX_REGISTERED_JOBS] = {};

            for (int32 i = 0; i < mJobsList.Count(); ++i)
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
            g_Error("Can't add job '%s', too many jobs %d", mJobsRegister->GetJobNameByFunction(function), mJobsList.Count());
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
                if (mJobsList.Count())
                {
                    DEBUG_ASSERT(!mHasSignal);
                    mSignalJobCount.Append();
                    mSignalJobCount[mSignalJobCount.Count() - 1].SetValue(mJobsList.Count() - mLastSignalJob);
                    mLastSignalJob = mJobsList.Count();
                    job_t& job     = mJobsList.Append();
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
                    job_t& job   = mJobsList.Append();
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
        DEBUG_ASSERT(mJobsList.Count() <= mDescr.MaxJobs + mNumSyncs * 2);
        DEBUG_ASSERT(mFetchLock.GetValue() == 0);

        mDone = false;
        mCurrentJob.SetValue(0);

        memset(&mDeferredStats, 0, sizeof(mDeferredStats));
        mDeferredStats.mNumExecutedJobs  = mJobsList.Count() - mNumSyncs * 2;
        mDeferredStats.mNumExecutedSyncs = mNumSyncs;
        mDeferredStats.mSubmitTime       = g_Microseconds();
        mDeferredStats.mStartTime        = 0;
        mDeferredStats.mEndTime          = 0;
        mDeferredStats.mWaitTime         = 0;

        if (mJobsList.Count() == 0)
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

        mSignalJobCount.Append();
        mSignalJobCount[mSignalJobCount.Count() - 1].SetValue(mJobsList.Count() - mLastSignalJob);

        job_t& job   = mJobsList.Append();
        job.function = Nop;
        job.data     = &JOB_LIST_DONE;

        if (mThreaded)
        {
            // hand over to the manager
            mManager->Submit(this, parallelism);
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
        if (mJobsList.Count() > 0)
        {
            // don't lock up but return if the job list was never properly submitted
            if (!DEBUG_VERIFY(!mDone && mSignalJobCount.Count() > 0))
            {
                return;
            }

            bool   waited    = false;
            uint64 waitStart = g_Microseconds();

            while (mSignalJobCount[mSignalJobCount.Count() - 1].GetValue() > 0)
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

            mJobsList.SetCount(0);
            mSignalJobCount.SetCount(0);
            mNumSyncs      = 0;
            mLastSignalJob = 0;

            uint64 waitEnd           = g_Microseconds();
            mDeferredStats.mWaitTime = waited ? (waitEnd - waitStart) : 0;
        }
        memcpy(&mStats, &mDeferredStats, sizeof(mStats));
        mDone = true;
    }

    bool JobsListInstance::TryWait()
    {
        if (mJobsList.Count() == 0 || mSignalJobCount[mSignalJobCount.Count() - 1].GetValue() <= 0)
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

    int32 JobsListInstance::RunJobsInternal(uint32 threadIndex, JobsListState_t& state, bool singleJob)
    {
        if (state.mVersion != mVersion.GetValue())
        {
            // trying to run an old mVersion of this list that is already mDone
            return STATE_DONE;
        }

        DEBUG_ASSERT(threadIndex < CONFIG_MAX_THREADS);

        if (mDeferredStats.mStartTime == 0)
        {
            mDeferredStats.mStartTime = g_Microseconds(); // first time any thread is running jobs from this list
        }

        int32 result = STATE_OK;

        do
        {
            // run through all signals and syncs before the last job that has been or is being executed
            // this loop is really an optimization to minimize the time spent in the mFetchLock section below
            for (; state.mLastJobIndex < (int32)mCurrentJob.GetValue() && state.mLastJobIndex < mJobsList.Count(); state.mLastJobIndex++)
            {
                if (mJobsList[state.mLastJobIndex].data == &JOB_SIGNAL)
                {
                    state.mSignalIndex++;
                    DEBUG_ASSERT(state.mSignalIndex < mSignalJobCount.Count());
                }
                else if (mJobsList[state.mLastJobIndex].data == &JOB_SYNCHRONIZE)
                {
                    DEBUG_ASSERT(state.mSignalIndex > 0);
                    if (mSignalJobCount[state.mSignalIndex - 1].GetValue() > 0)
                    {
                        // stalled on a synchronization point
                        return (result | STATE_STALLED);
                    }
                }
                else if (mJobsList[state.mLastJobIndex].data == &JOB_LIST_DONE)
                {
                    if (mSignalJobCount[mSignalJobCount.Count() - 1].GetValue() > 0)
                    {
                        // stalled on a synchronization point
                        return (result | STATE_STALLED);
                    }
                }
            }

            // try to lock to fetch a new job
            if (mFetchLock.Increment() == 1)
            {
                // grab a new job
                state.mNextJobIndex = mCurrentJob.Increment() - 1;

                // run through any remaining signals and syncs (this should rarely iterate more than once)
                for (; state.mLastJobIndex <= state.mNextJobIndex && state.mLastJobIndex < mJobsList.Count(); state.mLastJobIndex++)
                {
                    if (mJobsList[state.mLastJobIndex].data == &JOB_SIGNAL)
                    {
                        state.mSignalIndex++;
                        DEBUG_ASSERT(state.mSignalIndex < mSignalJobCount.Count());
                    }
                    else if (mJobsList[state.mLastJobIndex].data == &JOB_SYNCHRONIZE)
                    {
                        DEBUG_ASSERT(state.mSignalIndex > 0);
                        if (mSignalJobCount[state.mSignalIndex - 1].GetValue() > 0)
                        {
                            // return this job to the list
                            mCurrentJob.Decrement();
                            mFetchLock.Decrement();          // release the fetch lock
                            return (result | STATE_STALLED); // stalled on a synchronization point
                        }
                    }
                    else if (mJobsList[state.mLastJobIndex].data == &JOB_LIST_DONE)
                    {
                        if (mSignalJobCount[mSignalJobCount.Count() - 1].GetValue() > 0)
                        {
                            // return this job to the list
                            mCurrentJob.Decrement();
                            mFetchLock.Decrement();          // release the fetch lock
                            return (result | STATE_STALLED); // stalled on a synchronization point
                        }
                        // decrement the mDone count
                        mDoneGuards[mCurrentDoneGuard].Decrement();
                    }
                }
                mFetchLock.Decrement(); // release the fetch lock
            }
            else
            {
                mFetchLock.Decrement();          // release the fetch lock
                return (result | STATE_STALLED); // another thread is fetching right now so consider stalled
            }

            // if at the end of the job list we're mDone
            if (state.mNextJobIndex >= mJobsList.Count())
            {
                return (result | STATE_DONE);
            }

            // execute the next job
            {
                uint64 jobStart = g_Microseconds();

                mJobsList[state.mNextJobIndex].function(mJobsList[state.mNextJobIndex].data);

                uint64 jobEnd = g_Microseconds();
                mDeferredStats.mThreadExecTime[threadIndex] += jobEnd - jobStart;

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
                        g_Printf("%1.1f milliseconds for a single '%s' job from job list %s on thread %d\n", longJobTime, jobName, jobListName, mThreadIndex);
                    }
                }
#endif
            }

            result |= STATE_PROGRESS;

            // decrease the job count for the current signal
            if (mSignalJobCount[state.mSignalIndex].Decrement() == 0)
            {
                // if this was the very last job of the job list
                if (state.mSignalIndex == mSignalJobCount.Count() - 1)
                {
                    mDeferredStats.mEndTime = g_Microseconds();
                    return (result | STATE_DONE);
                }
            }

        } while (!singleJob);

        return result;
    }

    int32 JobsListInstance::RunJobs(uint32 threadIndex, JobsListState_t& state, bool singleJob)
    {
        int32  result = 0;
        uint64 start  = g_Microseconds();

        mNumThreadsExecuting.Increment();
        {
            result = RunJobsInternal(threadIndex, state, singleJob);
        }
        mNumThreadsExecuting.Decrement();

        mDeferredStats.mThreadTotalTime[threadIndex] += g_Microseconds() - start;
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

    JobsList::JobsList()
        : mJobsListInstance(nullptr)
    {
    }

    JobsList::JobsList(const JobsList& other)
        : mJobsListInstance(other.mJobsListInstance)
    {
    }

    JobsList::JobsList(JobsListInstance* instance)
        : mJobsListInstance(instance)
    {
        DEBUG_ASSERT(instance == nullptr || instance->mDescr.Priority > JOBSLIST_PRIORITY_NONE);
    }

    JobsList::~JobsList() 
    { 
        if (mJobsListInstance != nullptr)
        {
            Destruct(mJobsListInstance->mAllocator, mJobsListInstance);
            mJobsListInstance = nullptr;
        }
    }

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

    void JobsList::Submit(JobsList waitForJobList, int32 parallelism)
    {
        DEBUG_ASSERT(waitForJobList.mJobsListInstance != mJobsListInstance);
        mJobsListInstance->Submit((waitForJobList.mJobsListInstance != nullptr) ? waitForJobList.mJobsListInstance : nullptr, parallelism);
    }

    bool          JobsList::IsSubmitted() const { return mJobsListInstance->IsSubmitted(); }
    JobsListId_t  JobsList::GetId() const { return mJobsListInstance->GetId(); }
    const char*   JobsList::GetName() const { return mJobsListInstance->GetDescr().Name; }
    uint32        JobsList::GetColor() const { return mJobsListInstance->GetDescr().Color; }
    JobsListStats JobsList::GetStats() const { return JobsListStats(mJobsListInstance->GetStats()); }

    bool JobsList::operator==(const JobsList& other) const { return mJobsListInstance == other.mJobsListInstance; }
    bool JobsList::operator!=(const JobsList& other) const { return mJobsListInstance != other.mJobsListInstance; }
    bool JobsList::operator<(const JobsList& other) const { return mJobsListInstance->GetDescr().Priority < other.mJobsListInstance->GetDescr().Priority; }
    bool JobsList::operator>(const JobsList& other) const { return mJobsListInstance->GetDescr().Priority > other.mJobsListInstance->GetDescr().Priority; }
    bool JobsList::operator<=(const JobsList& other) const { return mJobsListInstance->GetDescr().Priority <= other.mJobsListInstance->GetDescr().Priority; }
    bool JobsList::operator>=(const JobsList& other) const { return mJobsListInstance->GetDescr().Priority >= other.mJobsListInstance->GetDescr().Priority; }

    const int32 JOB_THREAD_STACK_SIZE = 256 * 1024; // same size as the SPU local store

    JobThread::JobThread(JobsThreadDescr descr, bool* jobsPrioritize)
        : mDescr(descr)
        , mJobsPrioritize(jobsPrioritize)
        , mFirstJobList(0)
        , mLastJobList(0)
        , mThreadIndex(0)
    {
        for (int32 i = 0; i < CONFIG_MAX_JOBLISTS; ++i)
        {
            mJobListInstances[i] = nullptr;
            mJobListVersions[i]  = 0;
        }
    }

    JobThread::~JobThread() {}

    void JobThread::Start(uint16 _threadIndex)
    {
        mThreadIndex = _threadIndex;

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

    void JobThread::AddJobList(JobsListInstance* jobsList)
    {
        // must lock because multiple threads may try to add new job lists at the same time
        mAddJobMutex.Lock();
        {
            // wait until there is space available because in rare cases multiple versions of the same job lists may still be queued
            while (mLastJobList - mFirstJobList >= CONFIG_MAX_JOBLISTS)
            {
                csys::SysYield();
            }
            DEBUG_ASSERT(mLastJobList - mFirstJobList < CONFIG_MAX_JOBLISTS);
            mJobListInstances[mLastJobList & (CONFIG_MAX_JOBLISTS - 1)] = jobsList;
            mJobListVersions[mLastJobList & (CONFIG_MAX_JOBLISTS - 1)]  = jobsList->GetVersion();
            mLastJobList++;
        }
        mAddJobMutex.Unlock();
    }

    int32 JobThread::Run()
    {
        JobsListState_t threadJobListState[CONFIG_MAX_JOBLISTS];
        int16           numJobLists        = 0;
        int16           lastStalledJobList = -1;

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

            int16               currentJobList = 0;
            EJobsListPriority_t priority       = JOBSLIST_PRIORITY_NONE;
            if (lastStalledJobList < 0)
            {
                // find the job list with the highest priority
                for (int16 i = 0; i < numJobLists; i++)
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
                for (int16 i = 0; i < numJobLists; i++)
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
            const int32 result = threadJobListState[currentJobList].mJobsList->RunJobs(mThreadIndex, threadJobListState[currentJobList], singleJob);

            if ((result & JobsListInstance::STATE_DONE) != 0)
            {
                // done with this job list so remove it from the local list
                for (int16 i = currentJobList; i < numJobLists - 1; i++)
                {
                    threadJobListState[i] = threadJobListState[i + 1];
                }
                numJobLists--;
                lastStalledJobList = -1;
            }
            else if ((result & JobsListInstance::STATE_STALLED) != 0)
            {
                // yield when stalled on the same job list again without making any progress
                if (currentJobList == lastStalledJobList)
                {
                    if ((result & JobsListInstance::STATE_PROGRESS) == 0)
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

    JobsManagerLocal::JobsManagerLocal(Alloc* allocator)
        : mAllocator(allocator)
        , mJobsPrioritize(false)
        , mMaxThreads(0)
        , mJobLists()
        , mJobsRegister()
    {
        for (int32 i = 0; i < CONFIG_MAX_THREADS; i++)
        {
            mThreads[i] = nullptr;
        }
    }

    void JobsManagerLocal::Init(JobsThreadDescr threads[], int32 num)
    {
        DEBUG_ASSERT(num < CONFIG_MAX_THREADS);
        mJobLists.SetCapacity(mAllocator, CONFIG_MAX_JOBLISTS);

        for (int32 threadIndex = 0; threadIndex < num; threadIndex++)
        {
            mThreads[threadIndex] = Construct<JobThread>(mAllocator, TAG_JOBTHREAD, threads[threadIndex], &mJobsPrioritize);
            mThreads[threadIndex]->Start(threadIndex);
        }
        mMaxThreads = num;
    }

    void JobsManagerLocal::Shutdown()
    {
        for (int32 i = 0; i < CONFIG_MAX_THREADS; i++)
        {
            if (mThreads[i] != nullptr)
            {
                mThreads[i]->StopThread();
                Destruct(mAllocator, mThreads[i]);
                mThreads[i] = nullptr;
            }
        }
        mJobLists.SetCapacity(mAllocator, 0);
    }

    JobsList JobsManagerLocal::AllocJobList(JobsListDescr const& descr)
    {
        JobsListId_t       id      = mJobLists.Count();
        JobsListInstance*& jobList = mJobLists.Append();
        jobList                    = Construct<JobsListInstance>(mAllocator, TAG_JOBLIST, this, &mJobsRegister, mAllocator, id, descr);
        return JobsList(jobList);
    }

    void JobsManagerLocal::FreeJobList(JobsList& jobList)
    {
        if (jobList.mJobsListInstance == nullptr)
        {
            return;
        }

        // wait for all job threads to finish because job list deletion is not thread safe
        for (uint32 i = 0; i < mMaxThreads; i++)
        {
            mThreads[i]->WaitForThread();
        }

        int32 index = mJobLists.Find(jobList.mJobsListInstance);
        DEBUG_ASSERT(index >= 0 && mJobLists[index] == jobList.mJobsListInstance);
        mJobLists[index]->Wait();
        Destruct<JobsListInstance>(mAllocator, jobList.mJobsListInstance);
        mJobLists.Remove(index);

        // ok, job list instance has been released
        jobList.mJobsListInstance = nullptr;
    }

    int32    JobsManagerLocal::GetNumJobLists() const { return mJobLists.Count(); }
    int32    JobsManagerLocal::GetNumFreeJobLists() const { return CONFIG_MAX_JOBLISTS - mJobLists.Count(); }
    JobsList JobsManagerLocal::GetJobList(int32 index) { return JobsList(mJobLists[index]); }
    int32    JobsManagerLocal::GetNumProcessingUnits() const { return mMaxThreads; }

    void JobsManagerLocal::WaitForAllJobLists()
    {
        for (int32 i = 0; i < mJobLists.Count(); i++)
        {
            mJobLists[i]->Wait();
        }
    }

    void JobsManagerLocal::Submit(JobsListInstance* jobsList, int32 parallelism)
    {
        int32 numThreads = mMaxThreads;
        if (parallelism > numThreads)
            numThreads = mMaxThreads;
        else if (parallelism < 0)
            numThreads = mMaxThreads;

        if (numThreads > 0)
        {
            for (int32 i = 0; i < numThreads; i++)
            {
                mThreads[i]->AddJobList(jobsList);
                mThreads[i]->SignalWork();
            }
        }
        else
        {
            JobsListState_t state(jobsList->GetVersion());
            jobsList->RunJobs(0, state, false);
        }
    }

    bool        JobsManagerLocal::IsRegisteredJob(JobRun_t function) const { return mJobsRegister.IsRegisteredJob(function); }
    void        JobsManagerLocal::RegisterJob(JobRun_t function, const char* name) { mJobsRegister.RegisterJob(function, name); }
    const char* JobsManagerLocal::GetJobName(JobRun_t function) const { return mJobsRegister.GetJobNameByFunction(function); }

    JobsManager JobsManager::Create(Alloc* allocator)
    {
        JobsManagerLocal* manager = Construct<JobsManagerLocal>(allocator, TAG_JOBMANAGER, allocator);
        return JobsManager(manager);
    }

    void JobsManager::Destroy(JobsManager& manager)
    {
        JobsManagerLocal* localManager = static_cast<JobsManagerLocal*>(manager.mInstance);
        Destruct(localManager->mAllocator, localManager);
        manager.mInstance = nullptr;
    }

    // JobsManager

    void JobsManager::Init(JobsThreadDescr threads[], int32 num) { mInstance->Init(threads, num); }
    void JobsManager::Shutdown() { mInstance->Shutdown(); }

    JobsList JobsManager::AllocJobList(JobsListDescr const& descr) { return mInstance->AllocJobList(descr); }
    void     JobsManager::FreeJobList(JobsList& jobList) { mInstance->FreeJobList(jobList); }

    int32    JobsManager::GetNumJobLists() const { return mInstance->GetNumJobLists(); }
    int32    JobsManager::GetNumFreeJobLists() const { return mInstance->GetNumFreeJobLists(); }
    JobsList JobsManager::GetJobList(int32 index) { return mInstance->GetJobList(index); }

    int32 JobsManager::GetNumProcessingUnits() const { return mInstance->GetNumProcessingUnits(); }
    void  JobsManager::WaitForAllJobLists() { mInstance->WaitForAllJobLists(); }

    bool        JobsManager::IsRegisteredJob(JobRun_t function) const { return mInstance->IsRegisteredJob(function); }
    void        JobsManager::RegisterJob(JobRun_t function, const char* name) { mInstance->RegisterJob(function, name); }
    const char* JobsManager::GetJobName(JobRun_t function) const { return mInstance->GetJobName(function); }

    // JobsRegister

    JobsRegister::JobsRegister()
        : mNumRegisteredJobs(0)
    {
        for (int32 i = 0; i < CONFIG_MAX_REGISTERED_JOBS; i++)
        {
            mRegisteredJobFuncs[i] = nullptr;
            mRegisteredJobNames[i] = nullptr;
        }
    }

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

    const char* JobsRegister::GetJobNameByIndex(int32 index) const
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

    uint32 JobsListStats::GetNumExecutedJobs() const { return mStats->mNumExecutedJobs; }
    uint32 JobsListStats::GetNumSyncs() const { return mStats->mNumExecutedSyncs; }
    uint64 JobsListStats::GetSubmitTimeMicroSec() const { return mStats->mSubmitTime; }
    uint64 JobsListStats::GetStartTimeMicroSec() const { return mStats->mStartTime; }
    uint64 JobsListStats::GetFinishTimeMicroSec() const { return mStats->mEndTime; }
    uint64 JobsListStats::GetWaitTimeMicroSec() const { return mStats->mWaitTime; }

    uint64 JobsListStats::GetTotalProcessingTimeMicroSec() const
    {
        uint64 const* src   = &mStats->mThreadExecTime[0];
        uint64 const* end   = src + CONFIG_MAX_THREADS;
        uint64        total = *src++;
        do
        {
            total += *src++;
        } while (src < end);
        return total;
    }

    uint64 JobsListStats::GetTotalWastedTimeMicroSec() const
    {
        uint64 const* ttime = &mStats->mThreadTotalTime[0];
        uint64 const* end   = ttime + CONFIG_MAX_THREADS;
        uint64 const* etime = &mStats->mThreadExecTime[0];
        uint64        total = *ttime++ - *etime++;
        do
        {
            total += *ttime++ - *etime++;
        } while (ttime < end);
        return total;
    }

    uint64 JobsListStats::GetUnitProcessingTimeMicroSec(int32 unit) const
    {
        if (unit < 0 || unit >= CONFIG_MAX_THREADS)
            return 0;
        return mStats->mThreadExecTime[unit];
    }

    uint64 JobsListStats::GetUnitWastedTimeMicroSec(int32 unit) const
    {
        if (unit < 0 || unit >= CONFIG_MAX_THREADS)
            return 0;
        return mStats->mThreadTotalTime[unit] - mStats->mThreadExecTime[unit];
    }

} // namespace cjobs