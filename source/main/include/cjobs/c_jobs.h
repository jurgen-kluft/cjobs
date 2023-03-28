#ifndef __CJOBS_H__
#define __CJOBS_H__

namespace nsys
{
    typedef int                int32;
    typedef unsigned int       uint32;
    typedef unsigned long long uint64;

    struct InterlockedInteger
    {
        int32 GetValue();
        void SetValue(int32 value);
        int32 Increment();
        int32 Decrement();
    };

    void CPUCount(int32& logicalNum, int32& coreNum, int32& packageNum);

    typedef int32 core_t;

    namespace nthreading
    {
        class Thread
        {
        public:
            bool IsTerminating() const;
            void StopThread();
            void WaitForThread();
            void SignalWork();
        };

        void Yield();
    }

    uint64 Microseconds();
}

namespace nlib
{
    void Printf(const char* format, ...);
    void Error(const char* format, ...);
}

namespace njobs
{
    typedef void (*jobRun_t)(void*);
    void g_RegisterJob(jobRun_t function, const char* name);

    typedef int                int32;
    typedef unsigned int       uint32;
    typedef unsigned long long uint64;

    enum jobSyncType_t
    {
        SYNC_NONE,
        SYNC_SIGNAL,
        SYNC_SYNCHRONIZE
    };

    enum jobListId_t
    {
        JOBLIST_RENDERER_FRONTEND = 0,
        JOBLIST_RENDERER_BACKEND  = 1,
        JOBLIST_UTILITY           = 9, // won't print over-time warnings

        MAX_JOBLISTS = 32
    };

    enum jobListPriority_t
    {
        JOBLIST_PRIORITY_NONE,
        JOBLIST_PRIORITY_LOW,
        JOBLIST_PRIORITY_MEDIUM,
        JOBLIST_PRIORITY_HIGH
    };

    enum jobListParallelism_t
    {
        JOBLIST_PARALLELISM_DEFAULT     = -1, // use "jobs_numThreads" number of threads
        JOBLIST_PARALLELISM_MAX_CORES   = -2, // use a thread for each logical core (includes hyperthreads)
        JOBLIST_PARALLELISM_MAX_THREADS = -3  // use the maximum number of job threads, which can help if there is IO to overlap
    };

    class JobList
    {
        friend class JobManagerLocal;

    public:
        void AddJob(jobRun_t function, void* data);
        void InsertSyncPoint(jobSyncType_t syncType);

        // Submit the jobs in this list.
        void Submit(JobList* waitForJobList = nullptr, int parallelism = JOBLIST_PARALLELISM_DEFAULT);
        void Wait();              // Wait for the jobs in this list to finish. Will spin in place if any jobs are not done.
        bool TryWait();           // Try to wait for the jobs in this list to finish but either way return immediately. Returns true if all jobs are done.
        bool IsSubmitted() const; // returns true if the job list has been submitted.

        uint32       GetNumExecutedJobs() const;                    // Get the number of jobs executed in this job list.
        uint32       GetNumSyncs() const;                           // Get the number of sync points.
        uint64       GetSubmitTimeMicroSec() const;                 // Time at which the job list was submitted.
        uint64       GetStartTimeMicroSec() const;                  // Time at which execution of this job list started.
        uint64       GetFinishTimeMicroSec() const;                 // Time at which all jobs in the list were executed.
        uint64       GetWaitTimeMicroSec() const;                   // Time the host thread waited for this job list to finish.
        uint64       GetTotalProcessingTimeMicroSec() const;        // Get the total time all units spent processing this job list.
        uint64       GetTotalWastedTimeMicroSec() const;            // Get the total time all units wasted while processing this job list.
        uint64       GetUnitProcessingTimeMicroSec(int unit) const; // Time the given unit spent processing this job list.
        uint64       GetUnitWastedTimeMicroSec(int unit) const;     // Time the given unit wasted while processing this job list.
        jobListId_t  GetId() const;                                 // Get the job list ID
        const uint32 GetColor() const { return this->color; }       // Get the color for profiling.

    private:
        class JobListThreads* jobListThreads;
        const uint32          color;

        JobList(jobListId_t id, jobListPriority_t priority, uint32 maxJobs, uint32 maxSyncs, const uint32 color);
        ~JobList();
    };

    class JobsManager
    {
    public:
        virtual ~JobsManager() {}

        virtual void Init(int32 jobs_numThreads)     = 0;
        virtual void Shutdown() = 0;

        virtual JobList* AllocJobList(jobListId_t id, jobListPriority_t priority, uint32 maxJobs, uint32 maxSyncs, const uint32 color) = 0;
        virtual void     FreeJobList(JobList* jobList)                                                                                 = 0;

        virtual int      GetNumJobLists() const     = 0;
        virtual int      GetNumFreeJobLists() const = 0;
        virtual JobList* GetJobList(int index)      = 0;

        virtual int  GetNumProcessingUnits() = 0;
        virtual void WaitForAllJobLists()    = 0;
    };

    extern JobsManager* g_JobManager;

} // namespace njobs

#endif // __CJOBS_H__
