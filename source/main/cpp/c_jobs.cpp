#include "cjobs/c_jobs.h"
#include <assert.h>
#include <memory.h>

typedef int                      idSysMutex;
typedef nsys::InterlockedInteger SysInterlockedInteger;

namespace njobs
{
    static const int32 MAX_REGISTERED_JOBS = 128;

    struct registeredJob
    {
        jobRun_t    function;
        const char* name;
    } registeredJobs[MAX_REGISTERED_JOBS];

    static int32 numRegisteredJobs;

    const char* GetJobListName(jobListId_t id) { return jobNames[id]; }

    static bool IsRegisteredJob(jobRun_t function)
    {
        for (int32 i = 0; i < numRegisteredJobs; i++)
        {
            if (registeredJobs[i].function == function)
            {
                return true;
            }
        }
        return false;
    }

    void RegisterJob(jobRun_t function, const char* name)
    {
        if (IsRegisteredJob(function))
        {
            return;
        }
        registeredJobs[numRegisteredJobs].function = function;
        registeredJobs[numRegisteredJobs].name     = name;
        numRegisteredJobs++;
    }

    const char* GetJobName(jobRun_t function)
    {
        for (int32 i = 0; i < numRegisteredJobs; i++)
        {
            if (registeredJobs[i].function == function)
            {
                return registeredJobs[i].name;
            }
        }
        return "unknown";
    }

    const static int32 MAX_THREADS = 32;

    struct threadJobListState_t
    {
        threadJobListState_t()
            : jobList(nullptr)
            , version(0xFFFFFFFF)
            , signalIndex(0)
            , lastJobIndex(0)
            , nextJobIndex(-1)
        {
        }
        threadJobListState_t(int32 _version)
            : jobList(nullptr)
            , version(_version)
            , signalIndex(0)
            , lastJobIndex(0)
            , nextJobIndex(-1)
        {
        }
        JobListThreads* jobList;
        int32           version;
        int32           signalIndex;
        int32           lastJobIndex;
        int32           nextJobIndex;
    };

    struct threadStats_t
    {
        uint32 numExecutedJobs;
        uint32 numExecutedSyncs;
        uint64 submitTime;
        uint64 startTime;
        uint64 endTime;
        uint64 waitTime;
        uint64 threadExecTime[MAX_THREADS];
        uint64 threadTotalTime[MAX_THREADS];
    };

    class JobListThreads
    {
    public:
        JobListThreads(jobListId_t id, jobListPriority_t priority, uint32 maxJobs, uint32 maxSyncs);
        ~JobListThreads();

        // These are called from the one thread that manages this list.
        inline void AddJob(jobRun_t function, void* data);
        inline void InsertSyncPoint(jobSyncType_t syncType);
        void        Submit(JobListThreads* waitForJobList_, int32 parallelism);
        void        Wait();
        bool        TryWait();
        bool        IsSubmitted() const;

        uint32 GetNumExecutedJobs() const { return threadStats.numExecutedJobs; }
        uint32 GetNumSyncs() const { return threadStats.numExecutedSyncs; }
        uint64 GetSubmitTimeMicroSec() const { return threadStats.submitTime; }
        uint64 GetStartTimeMicroSec() const { return threadStats.startTime; }
        uint64 GetFinishTimeMicroSec() const { return threadStats.endTime; }
        uint64 GetWaitTimeMicroSec() const { return threadStats.waitTime; }
        uint64 GetTotalProcessingTimeMicroSec() const;
        uint64 GetTotalWastedTimeMicroSec() const;
        uint64 GetUnitProcessingTimeMicroSec(int32 unit) const;
        uint64 GetUnitWastedTimeMicroSec(int32 unit) const;

        jobListId_t       GetId() const { return listId; }
        jobListPriority_t GetPriority() const { return listPriority; }
        int32             GetVersion() { return version.GetValue(); }

        bool WaitForOtherJobList();

        // This is thread safe and called from the job threads.
        enum runResult_t
        {
            RUN_OK       = 0,
            RUN_PROGRESS = 1 << 0,
            RUN_DONE     = 1 << 1,
            RUN_STALLED  = 1 << 2
        };

        int32 RunJobs(uint32 threadNum, threadJobListState_t& state, bool singleJob);

    private:
        static const int32 NUM_DONE_GUARDS = 4; // cycle through 4 guards so we can cyclicly chain job lists

        bool                   threaded;
        bool                   done;
        bool                   hasSignal;
        jobListId_t            listId;
        jobListPriority_t      listPriority;
        uint32                 maxJobs;
        uint32                 maxSyncs;
        uint32                 numSyncs;
        int32                  lastSignalJob;
        SysInterlockedInteger* waitForGuard;
        SysInterlockedInteger  doneGuards[NUM_DONE_GUARDS];
        int32                  currentDoneGuard;
        SysInterlockedInteger  version;
        struct job_t
        {
            jobRun_t function;
            void*    data;
            int32    executed;
        };
        idList<job_t, TAG_JOBLIST>                 jobList;
        idList<SysInterlockedInteger, TAG_JOBLIST> signalJobCount;
        SysInterlockedInteger                      currentJob;
        SysInterlockedInteger                      fetchLock;
        SysInterlockedInteger                      numThreadsExecuting;

        threadStats_t deferredThreadStats;
        threadStats_t threadStats;

        int32 RunJobsInternal(uint32 threadNum, threadJobListState_t& state, bool singleJob);

        static void Nop(void* data) {}

        static int32 JOB_SIGNAL;
        static int32 JOB_SYNCHRONIZE;
        static int32 JOB_LIST_DONE;
    };

    int32 JobListThreads::JOB_SIGNAL;
    int32 JobListThreads::JOB_SYNCHRONIZE;
    int32 JobListThreads::JOB_LIST_DONE;

    JobListThreads::JobListThreads(jobListId_t id, jobListPriority_t priority, uint32 maxJobs, uint32 maxSyncs)
        : threaded(true)
        , done(true)
        , hasSignal(false)
        , listId(id)
        , listPriority(priority)
        , numSyncs(0)
        , lastSignalJob(0)
        , waitForGuard(nullptr)
        , currentDoneGuard(0)
        , jobList()
    {

        assert(listPriority != JOBLIST_PRIORITY_NONE);

        this->maxJobs  = maxJobs;
        this->maxSyncs = maxSyncs;
        jobList.AssureSize(maxJobs + maxSyncs * 2 + 1); // syncs go in as dummy jobs and one more to update the doneCount
        jobList.SetNum(0);
        signalJobCount.AssureSize(maxSyncs + 1); // need one extra for submit
        signalJobCount.SetNum(0);

        memset(&deferredThreadStats, 0, sizeof(threadStats_t));
        memset(&threadStats, 0, sizeof(threadStats_t));
    }

    JobListThreads::~JobListThreads() { Wait(); }

    inline void JobListThreads::AddJob(jobRun_t function, void* data)
    {
        assert(done);
#if defined(_DEBUG)
        // make sure there isn't already a job with the same function and data in the list
        if (jobList.Num() < 1000)
        { // don't do this N^2 slow check on big lists
            for (int32 i = 0; i < jobList.Num(); i++)
            {
                assert(jobList[i].function != function || jobList[i].data != data);
            }
        }
#endif
        if (1)
        { // JDC: this never worked in tech5!  !jobList.IsFull() ) {
            job_t& job   = jobList.Alloc();
            job.function = function;
            job.data     = data;
            job.executed = 0;
        }
        else
        {
            // debug output to show us what is overflowing
            int32 currentJobCount[MAX_REGISTERED_JOBS] = {};

            for (int32 i = 0; i < jobList.Num(); ++i)
            {
                const char* jobName = GetJobName(jobList[i].function);
                for (int32 j = 0; j < numRegisteredJobs; ++j)
                {
                    if (jobName == registeredJobs[j].name)
                    {
                        currentJobCount[j]++;
                        break;
                    }
                }
            }

            // print the quantity of each job type
            for (int32 i = 0; i < numRegisteredJobs; ++i)
            {
                if (currentJobCount[i] > 0)
                {
                    nlib::Printf("Job: %s, # %d", registeredJobs[i].name, currentJobCount[i]);
                }
            }
            nlib::Error("Can't add job '%s', too many jobs %d", GetJobName(function), jobList.Num());
        }
    }

    inline void JobListThreads::InsertSyncPoint(jobSyncType_t syncType)
    {
        assert(done);
        switch (syncType)
        {
            case SYNC_SIGNAL:
            {
                assert(!hasSignal);
                if (jobList.Num())
                {
                    assert(!hasSignal);
                    signalJobCount.Alloc();
                    signalJobCount[signalJobCount.Num() - 1].SetValue(jobList.Num() - lastSignalJob);
                    lastSignalJob = jobList.Num();
                    job_t& job    = jobList.Alloc();
                    job.function  = Nop;
                    job.data      = &JOB_SIGNAL;
                    hasSignal     = true;
                }
                break;
            }
            case SYNC_SYNCHRONIZE:
            {
                if (hasSignal)
                {
                    job_t& job   = jobList.Alloc();
                    job.function = Nop;
                    job.data     = &JOB_SYNCHRONIZE;
                    hasSignal    = false;
                    numSyncs++;
                }
                break;
            }
        }
    }

    void JobListThreads::Submit(JobListThreads* waitForJobList, int32 parallelism)
    {
        assert(done);
        assert(numSyncs <= maxSyncs);
        assert((uint32)jobList.Num() <= maxJobs + numSyncs * 2);
        assert(fetchLock.GetValue() == 0);

        done = false;
        currentJob.SetValue(0);

        memset(&deferredThreadStats, 0, sizeof(deferredThreadStats));
        deferredThreadStats.numExecutedJobs  = jobList.Num() - numSyncs * 2;
        deferredThreadStats.numExecutedSyncs = numSyncs;
        deferredThreadStats.submitTime       = nsys::Microseconds();
        deferredThreadStats.startTime        = 0;
        deferredThreadStats.endTime          = 0;
        deferredThreadStats.waitTime         = 0;

        if (jobList.Num() == 0)
        {
            return;
        }

        if (waitForJobList != nullptr)
        {
            waitForGuard = &waitForJobList->doneGuards[waitForJobList->currentDoneGuard];
        }
        else
        {
            waitForGuard = nullptr;
        }

        currentDoneGuard = (currentDoneGuard + 1) & (NUM_DONE_GUARDS - 1);
        doneGuards[currentDoneGuard].SetValue(1);

        signalJobCount.Alloc();
        signalJobCount[signalJobCount.Num() - 1].SetValue(jobList.Num() - lastSignalJob);

        job_t& job   = jobList.Alloc();
        job.function = Nop;
        job.data     = &JOB_LIST_DONE;

        if (threaded)
        {
            // hand over to the manager
            void SubmitJobList(JobListThreads * jobList, int32 parallelism);
            SubmitJobList(this, parallelism);
        }
        else
        {
            // run all the jobs right here
            threadJobListState_t state(GetVersion());
            RunJobs(0, state, false);
        }
    }

    void JobListThreads::Wait()
    {
        if (jobList.Num() > 0)
        {
            // don't lock up but return if the job list was never properly submitted
            if (!verify(!done && signalJobCount.Num() > 0))
            {
                return;
            }

            bool   waited    = false;
            uint64 waitStart = nsys::Microseconds();

            while (signalJobCount[signalJobCount.Num() - 1].GetValue() > 0)
            {
                nsys::nthreading::Yield();
                waited = true;
            }
            version.Increment();
            while (numThreadsExecuting.GetValue() > 0)
            {
                nsys::nthreading::Yield();
                waited = true;
            }

            jobList.SetNum(0);
            signalJobCount.SetNum(0);
            numSyncs      = 0;
            lastSignalJob = 0;

            uint64 waitEnd               = nsys::Microseconds();
            deferredThreadStats.waitTime = waited ? (waitEnd - waitStart) : 0;
        }
        memcpy(&threadStats, &deferredThreadStats, sizeof(threadStats));
        done = true;
    }

    bool JobListThreads::TryWait()
    {
        if (jobList.Num() == 0 || signalJobCount[signalJobCount.Num() - 1].GetValue() <= 0)
        {
            Wait();
            return true;
        }
        return false;
    }

    bool JobListThreads::IsSubmitted() const { return !done; }

    uint64 JobListThreads::GetTotalProcessingTimeMicroSec() const
    {
        uint64 total = 0;
        for (int32 unit = 0; unit < MAX_THREADS; unit++)
        {
            total += threadStats.threadExecTime[unit];
        }
        return total;
    }

    uint64 JobListThreads::GetTotalWastedTimeMicroSec() const
    {
        uint64 total = 0;
        for (int32 unit = 0; unit < MAX_THREADS; unit++)
        {
            total += threadStats.threadTotalTime[unit] - threadStats.threadExecTime[unit];
        }
        return total;
    }

    uint64 JobListThreads::GetUnitProcessingTimeMicroSec(int32 unit) const
    {
        if (unit < 0 || unit >= MAX_THREADS)
        {
            return 0;
        }
        return threadStats.threadExecTime[unit];
    }

    uint64 JobListThreads::GetUnitWastedTimeMicroSec(int32 unit) const
    {
        if (unit < 0 || unit >= MAX_THREADS)
        {
            return 0;
        }
        return threadStats.threadTotalTime[unit] - threadStats.threadExecTime[unit];
    }

#ifndef _DEBUG
    volatile float    longJobTime;
    volatile jobRun_t longJobFunc;
    volatile void*    longJobData;
#endif

    int32 JobListThreads::RunJobsInternal(uint32 threadNum, threadJobListState_t& state, bool singleJob)
    {
        if (state.version != version.GetValue())
        {
            // trying to run an old version of this list that is already done
            return RUN_DONE;
        }

        assert(threadNum < MAX_THREADS);

        if (deferredThreadStats.startTime == 0)
        {
            deferredThreadStats.startTime = nsys::Microseconds(); // first time any thread is running jobs from this list
        }

        int32 result = RUN_OK;

        do
        {
            // run through all signals and syncs before the last job that has been or is being executed
            // this loop is really an optimization to minimize the time spent in the fetchLock section below
            for (; state.lastJobIndex < (int32)currentJob.GetValue() && state.lastJobIndex < jobList.Num(); state.lastJobIndex++)
            {
                if (jobList[state.lastJobIndex].data == &JOB_SIGNAL)
                {
                    state.signalIndex++;
                    assert(state.signalIndex < signalJobCount.Num());
                }
                else if (jobList[state.lastJobIndex].data == &JOB_SYNCHRONIZE)
                {
                    assert(state.signalIndex > 0);
                    if (signalJobCount[state.signalIndex - 1].GetValue() > 0)
                    {
                        // stalled on a synchronization point
                        return (result | RUN_STALLED);
                    }
                }
                else if (jobList[state.lastJobIndex].data == &JOB_LIST_DONE)
                {
                    if (signalJobCount[signalJobCount.Num() - 1].GetValue() > 0)
                    {
                        // stalled on a synchronization point
                        return (result | RUN_STALLED);
                    }
                }
            }

            // try to lock to fetch a new job
            if (fetchLock.Increment() == 1)
            {
                // grab a new job
                state.nextJobIndex = currentJob.Increment() - 1;

                // run through any remaining signals and syncs (this should rarely iterate more than once)
                for (; state.lastJobIndex <= state.nextJobIndex && state.lastJobIndex < jobList.Num(); state.lastJobIndex++)
                {
                    if (jobList[state.lastJobIndex].data == &JOB_SIGNAL)
                    {
                        state.signalIndex++;
                        assert(state.signalIndex < signalJobCount.Num());
                    }
                    else if (jobList[state.lastJobIndex].data == &JOB_SYNCHRONIZE)
                    {
                        assert(state.signalIndex > 0);
                        if (signalJobCount[state.signalIndex - 1].GetValue() > 0)
                        {
                            // return this job to the list
                            currentJob.Decrement();
                            // release the fetch lock
                            fetchLock.Decrement();
                            // stalled on a synchronization point
                            return (result | RUN_STALLED);
                        }
                    }
                    else if (jobList[state.lastJobIndex].data == &JOB_LIST_DONE)
                    {
                        if (signalJobCount[signalJobCount.Num() - 1].GetValue() > 0)
                        {
                            // return this job to the list
                            currentJob.Decrement();
                            // release the fetch lock
                            fetchLock.Decrement();
                            // stalled on a synchronization point
                            return (result | RUN_STALLED);
                        }
                        // decrement the done count
                        doneGuards[currentDoneGuard].Decrement();
                    }
                }
                // release the fetch lock
                fetchLock.Decrement();
            }
            else
            {
                // release the fetch lock
                fetchLock.Decrement();
                // another thread is fetching right now so consider stalled
                return (result | RUN_STALLED);
            }

            // if at the end of the job list we're done
            if (state.nextJobIndex >= jobList.Num())
            {
                return (result | RUN_DONE);
            }

            // execute the next job
            {
                uint64 jobStart = nsys::Microseconds();

                jobList[state.nextJobIndex].function(jobList[state.nextJobIndex].data);
                jobList[state.nextJobIndex].executed = 1;

                uint64 jobEnd = nsys::Microseconds();
                deferredThreadStats.threadExecTime[threadNum] += jobEnd - jobStart;

#ifndef _DEBUG
                if (jobs_longJobMicroSec.GetInteger() > 0)
                {
                    if (jobEnd - jobStart > jobs_longJobMicroSec.GetInteger() && GetId() != JOBLIST_UTILITY)
                    {
                        longJobTime             = (jobEnd - jobStart) * (1.0f / 1000.0f);
                        longJobFunc             = jobList[state.nextJobIndex].function;
                        longJobData             = jobList[state.nextJobIndex].data;
                        const char* jobName     = GetJobName(jobList[state.nextJobIndex].function);
                        const char* jobListName = GetJobListName(GetId());
                        nlib::Printf("%1.1f milliseconds for a single '%s' job from job list %s on thread %d\n", longJobTime, jobName, jobListName, threadNum);
                    }
                }
#endif
            }

            result |= RUN_PROGRESS;

            // decrease the job count for the current signal
            if (signalJobCount[state.signalIndex].Decrement() == 0)
            {
                // if this was the very last job of the job list
                if (state.signalIndex == signalJobCount.Num() - 1)
                {
                    deferredThreadStats.endTime = nsys::Microseconds();
                    return (result | RUN_DONE);
                }
            }

        } while (!singleJob);

        return result;
    }

    int32 JobListThreads::RunJobs(uint32 threadNum, threadJobListState_t& state, bool singleJob)
    {
        uint64 start = nsys::Microseconds();

        numThreadsExecuting.Increment();

        int32 result = RunJobsInternal(threadNum, state, singleJob);

        numThreadsExecuting.Decrement();

        deferredThreadStats.threadTotalTime[threadNum] += nsys::Microseconds() - start;

        return result;
    }

    bool JobListThreads::WaitForOtherJobList()
    {
        if (waitForGuard != nullptr)
        {
            if (waitForGuard->GetValue() > 0)
            {
                return true;
            }
        }
        return false;
    }

    JobList::JobList(jobListId_t id, jobListPriority_t priority, uint32 maxJobs, uint32 maxSyncs, const uint32 color)
    {
        assert(priority > JOBLIST_PRIORITY_NONE);
        this->jobListThreads = new (TAG_JOBLIST) JobListThreads(id, priority, maxJobs, maxSyncs);
        this->color          = color;
    }

    JobList::~JobList() { delete jobListThreads; }

    void JobList::AddJob(jobRun_t function, void* data)
    {
        assert(IsRegisteredJob(function));
        jobListThreads->AddJob(function, data);
    }

    void JobList::InsertSyncPoint(jobSyncType_t syncType) { jobListThreads->InsertSyncPoint(syncType); }

    void JobList::Wait()
    {
        if (jobListThreads != nullptr)
        {
            jobListThreads->Wait();
        }
    }

    bool JobList::TryWait()
    {
        bool done = true;
        if (jobListThreads != nullptr)
        {
            done &= jobListThreads->TryWait();
        }
        return done;
    }

    void JobList::Submit(JobList* waitForJobList, int32 parallelism)
    {
        assert(waitForJobList != this);
        jobListThreads->Submit((waitForJobList != nullptr) ? waitForJobList->jobListThreads : nullptr, parallelism);
    }

    bool JobList::IsSubmitted() const { return jobListThreads->IsSubmitted(); }

    uint32 JobList::GetNumExecutedJobs() const { return jobListThreads->GetNumExecutedJobs(); }
    uint32 JobList::GetNumSyncs() const { return jobListThreads->GetNumSyncs(); }

    uint64 JobList::GetSubmitTimeMicroSec() const { return jobListThreads->GetSubmitTimeMicroSec(); }
    uint64 JobList::GetStartTimeMicroSec() const { return jobListThreads->GetStartTimeMicroSec(); }
    uint64 JobList::GetFinishTimeMicroSec() const { return jobListThreads->GetFinishTimeMicroSec(); }
    uint64 JobList::GetWaitTimeMicroSec() const { return jobListThreads->GetWaitTimeMicroSec(); }
    uint64 JobList::GetTotalProcessingTimeMicroSec() const { return jobListThreads->GetTotalProcessingTimeMicroSec(); }
    uint64 JobList::GetTotalWastedTimeMicroSec() const { return jobListThreads->GetTotalWastedTimeMicroSec(); }
    uint64 JobList::GetUnitProcessingTimeMicroSec(int32 unit) const { return jobListThreads->GetUnitProcessingTimeMicroSec(unit); }
    uint64 JobList::GetUnitWastedTimeMicroSec(int32 unit) const { return jobListThreads->GetUnitWastedTimeMicroSec(unit); }

    jobListId_t JobList::GetId() const { return jobListThreads->GetId(); }

    const int32 JOB_THREAD_STACK_SIZE = 256 * 1024; // same size as the SPU local store

    struct threadJobList_t
    {
        JobListThreads* jobList;
        int32           version;
    };

    class JobThread : public nsys::nthreading::Thread
    {
    public:
        JobThread();
        ~JobThread();

        void Start(nsys::core_t core, uint32 threadNum);

        void AddJobList(JobListThreads* jobList);

    private:
        threadJobList_t jobLists[MAX_JOBLISTS]; // cyclic buffer with job lists
        uint32          firstJobList;           // index of the last job list the thread grabbed
        uint32          lastJobList;            // index where the next job list to work on will be added
        idSysMutex      addJobMutex;

        uint32 threadNum;

        virtual int32 Run();
    };

    JobThread::JobThread()
        : firstJobList(0)
        , lastJobList(0)
        , threadNum(0)
    {
    }

    JobThread::~JobThread() {}

    void JobThread::Start(nsys::core_t core, uint32 threadNum)
    {
        this->threadNum = threadNum;
        StartWorkerThread(va("JobListProcessor_%d", threadNum), core, THREAD_NORMAL, JOB_THREAD_STACK_SIZE);
    }

    void JobThread::AddJobList(JobListThreads* jobList)
    {
        // must lock because multiple threads may try to add new job lists at the same time
        addJobMutex.Lock();
        // wait until there is space available because in rare cases multiple versions of the same job lists may still be queued
        while (lastJobList - firstJobList >= MAX_JOBLISTS)
        {
            nsys::nthreading::Yield();
        }
        assert(lastJobList - firstJobList < MAX_JOBLISTS);
        jobLists[lastJobList & (MAX_JOBLISTS - 1)].jobList = jobList;
        jobLists[lastJobList & (MAX_JOBLISTS - 1)].version = jobList->GetVersion();
        lastJobList++;
        addJobMutex.Unlock();
    }

    int32 JobThread::Run()
    {
        threadJobListState_t threadJobListState[MAX_JOBLISTS];
        int32                numJobLists        = 0;
        int32                lastStalledJobList = -1;

        while (!IsTerminating())
        {

            // fetch any new job lists and add them to the local list
            if (numJobLists < MAX_JOBLISTS && firstJobList < lastJobList)
            {
                threadJobListState[numJobLists].jobList      = jobLists[firstJobList & (MAX_JOBLISTS - 1)].jobList;
                threadJobListState[numJobLists].version      = jobLists[firstJobList & (MAX_JOBLISTS - 1)].version;
                threadJobListState[numJobLists].signalIndex  = 0;
                threadJobListState[numJobLists].lastJobIndex = 0;
                threadJobListState[numJobLists].nextJobIndex = -1;
                numJobLists++;
                firstJobList++;
            }
            if (numJobLists == 0)
            {
                break;
            }

            int32             currentJobList = 0;
            jobListPriority_t priority       = JOBLIST_PRIORITY_NONE;
            if (lastStalledJobList < 0)
            {
                // find the job list with the highest priority
                for (int32 i = 0; i < numJobLists; i++)
                {
                    if (threadJobListState[i].jobList->GetPriority() > priority && !threadJobListState[i].jobList->WaitForOtherJobList())
                    {
                        priority       = threadJobListState[i].jobList->GetPriority();
                        currentJobList = i;
                    }
                }
            }
            else
            {
                // try to hide the stall with a job from a list that has equal or higher priority
                currentJobList = lastStalledJobList;
                priority       = threadJobListState[lastStalledJobList].jobList->GetPriority();
                for (int32 i = 0; i < numJobLists; i++)
                {
                    if (i != lastStalledJobList && threadJobListState[i].jobList->GetPriority() >= priority && !threadJobListState[i].jobList->WaitForOtherJobList())
                    {
                        priority       = threadJobListState[i].jobList->GetPriority();
                        currentJobList = i;
                    }
                }
            }

            // if the priority is high then try to run through the whole list to reduce the overhead
            // otherwise run a single job and re-evaluate priorities for the next job
            bool singleJob = (priority == JOBLIST_PRIORITY_HIGH) ? false : jobs_prioritize.GetBool();

            // try running one or more jobs from the current job list
            int32 result = threadJobListState[currentJobList].jobList->RunJobs(threadNum, threadJobListState[currentJobList], singleJob);

            if ((result & JobListThreads::RUN_DONE) != 0)
            {
                // done with this job list so remove it from the local list
                for (int32 i = currentJobList; i < numJobLists - 1; i++)
                {
                    threadJobListState[i] = threadJobListState[i + 1];
                }
                numJobLists--;
                lastStalledJobList = -1;
            }
            else if ((result & JobListThreads::RUN_STALLED) != 0)
            {
                // yield when stalled on the same job list again without making any progress
                if (currentJobList == lastStalledJobList)
                {
                    if ((result & JobListThreads::RUN_PROGRESS) == 0)
                    {
                        nsys::nthreading::Yield();
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

#define MAX_JOB_THREADS 2
#define NUM_JOB_THREADS "2"
#define JOB_THREAD_CORES                                                                                                                                                                                                                                \
    {                                                                                                                                                                                                                                                   \
        CORE_ANY, CORE_ANY, CORE_ANY, CORE_ANY, CORE_ANY, CORE_ANY, CORE_ANY, CORE_ANY, CORE_ANY, CORE_ANY, CORE_ANY, CORE_ANY, CORE_ANY, CORE_ANY, CORE_ANY, CORE_ANY, CORE_ANY, CORE_ANY, CORE_ANY, CORE_ANY, CORE_ANY, CORE_ANY, CORE_ANY, CORE_ANY, \
            CORE_ANY, CORE_ANY, CORE_ANY, CORE_ANY, CORE_ANY, CORE_ANY, CORE_ANY, CORE_ANY                                                                                                                                                              \
    }

    class JobsManagerLocal : public JobsManager
    {
    public:
        virtual ~JobsManagerLocal() {}

        virtual void Init(int32 jobs_numThreads);
        virtual void Shutdown();

        virtual JobList* AllocJobList(jobListId_t id, jobListPriority_t priority, uint32 maxJobs, uint32 maxSyncs, const uint32 color);
        virtual void     FreeJobList(JobList* jobList);

        virtual int32    GetNumJobLists() const;
        virtual int32    GetNumFreeJobLists() const;
        virtual JobList* GetJobList(int32 index);

        virtual int32 GetNumProcessingUnits();

        virtual void WaitForAllJobLists();

        void Submit(JobListThreads* jobList, int32 parallelism);

    private:
        JobThread                          threads[MAX_JOB_THREADS];
        uint32                               maxThreads;
        int32                                numPhysicalCpuCores;
        int32                                numLogicalCpuCores;
        int32                                numCpuPackages;
        idStaticList<JobList*, MAX_JOBLISTS> jobLists;
    };

    JobsManagerLocal g_JobManagerLocal;
    JobsManager*     g_JobManager = &g_JobManagerLocal;

    void SubmitJobList(JobListThreads* jobList, int32 parallelism) { g_JobManagerLocal.Submit(jobList, parallelism); }

    void JobsManagerLocal::Init(int32 jobs_numThreads)
    {
        // on consoles this will have specific cores for the threads, but on PC they will all be CORE_ANY
        nsys::core_t cores[] = JOB_THREAD_CORES;
        assert(sizeof(cores) / sizeof(cores[0]) >= MAX_JOB_THREADS);

        for (int32 i = 0; i < MAX_JOB_THREADS; i++)
        {
            threads[i].Start(cores[i], i);
        }
        maxThreads = jobs_numThreads;

        nsys::CPUCount(numPhysicalCpuCores, numLogicalCpuCores, numCpuPackages);
    }

    void JobsManagerLocal::Shutdown()
    {
        for (int32 i = 0; i < MAX_JOB_THREADS; i++)
        {
            threads[i].StopThread();
        }
    }

    JobList* JobsManagerLocal::AllocJobList(jobListId_t id, jobListPriority_t priority, uint32 maxJobs, uint32 maxSyncs, const uint32 color)
    {
        for (int32 i = 0; i < jobLists.Num(); i++)
        {
            if (jobLists[i]->GetId() == id)
            {
                // idStudio may cause job lists to be allocated multiple times
            }
        }
        JobList* jobList = new (TAG_JOBLIST) JobList(id, priority, maxJobs, maxSyncs, color);
        jobLists.Append(jobList);
        return jobList;
    }

    void JobsManagerLocal::FreeJobList(JobList* jobList)
    {
        if (jobList == nullptr)
        {
            return;
        }
        // wait for all job threads to finish because job list deletion is not thread safe
        for (uint32 i = 0; i < maxThreads; i++)
        {
            threads[i].WaitForThread();
        }
        int32 index = jobLists.FindIndex(jobList);
        assert(index >= 0 && jobLists[index] == jobList);
        jobLists[index]->Wait();
        delete jobLists[index];
        jobLists.RemoveIndexFast(index);
    }

    int32    JobsManagerLocal::GetNumJobLists() const { return jobLists.Num(); }
    int32    JobsManagerLocal::GetNumFreeJobLists() const { return MAX_JOBLISTS - jobLists.Num(); }
    JobList* JobsManagerLocal::GetJobList(int32 index) { return jobLists[index]; }
    int32    JobsManagerLocal::GetNumProcessingUnits() { return maxThreads; }

    void JobsManagerLocal::WaitForAllJobLists()
    {
        // wait for all job lists to complete
        for (int32 i = 0; i < jobLists.Num(); i++)
        {
            jobLists[i]->Wait();
        }
    }

    void JobsManagerLocal::Submit(JobListThreads* jobList, int32 parallelism)
    {
        // determine the number of threads to use
        int32 numThreads = maxThreads;
        if (parallelism == JOBLIST_PARALLELISM_DEFAULT)
        {
            numThreads = maxThreads;
        }
        else if (parallelism == JOBLIST_PARALLELISM_MAX_CORES)
        {
            numThreads = numLogicalCpuCores;
        }
        else if (parallelism == JOBLIST_PARALLELISM_MAX_THREADS)
        {
            numThreads = MAX_JOB_THREADS;
        }
        else if (parallelism > MAX_JOB_THREADS)
        {
            numThreads = MAX_JOB_THREADS;
        }
        else
        {
            numThreads = parallelism;
        }

        if (numThreads <= 0)
        {
            threadJobListState_t state(jobList->GetVersion());
            jobList->RunJobs(0, state, false);
            return;
        }

        for (int32 i = 0; i < numThreads; i++)
        {
            threads[i].AddJobList(jobList);
            threads[i].SignalWork();
        }
    }

} // namespace njobs