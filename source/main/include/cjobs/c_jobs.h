#ifndef __CJOBS_H__
#define __CJOBS_H__

namespace njobs
{
    typedef int                int32;
    typedef unsigned int       uint32;
    typedef unsigned long long uint64;

    class Alloc;

    typedef void (*JobRun_t)(void*);

    enum EJobSyncType_t
    {
        SYNC_NONE,
        SYNC_SIGNAL,
        SYNC_SYNCHRONIZE
    };

    typedef int32 JobListId_t;

    enum EJobListPriority_t
    {
        JOBLIST_PRIORITY_NONE,
        JOBLIST_PRIORITY_LOW,
        JOBLIST_PRIORITY_MEDIUM,
        JOBLIST_PRIORITY_HIGH
    };

    enum EJobListParallelism_t
    {
        JOBLIST_PARALLELISM_DEFAULT     = -1, // use "jobs_numThreads" number of threads
        JOBLIST_PARALLELISM_MAX_CORES   = -2, // use a thread for each logical core (includes hyperthreads)
        JOBLIST_PARALLELISM_MAX_THREADS = -3  // use the maximum number of job threads, which can help if there is IO to overlap
    };

    enum EJobManagerConfig
    {
        CONFIG_MAX_REGISTERED_JOBS = 128,
        CONFIG_MAX_THREADS         = 32,
        CONFIG_MAX_JOBTHREADS      = 32,
        CONFIG_MAX_JOBLISTS        = 32,
    };

    struct ThreadStats_t
    {
        uint32 GetNumExecutedJobs() const;                    // Get the number of jobs executed in this job list.
        uint32 GetNumSyncs() const;                           // Get the number of sync points.
        uint64 GetSubmitTimeMicroSec() const;                 // Time at which the job list was submitted.
        uint64 GetStartTimeMicroSec() const;                  // Time at which execution of this job list started.
        uint64 GetFinishTimeMicroSec() const;                 // Time at which all jobs in the list were executed.
        uint64 GetWaitTimeMicroSec() const;                   // Time the host thread waited for this job list to finish.
        uint64 GetTotalProcessingTimeMicroSec() const;        // Get the total time all units spent processing this job list.
        uint64 GetTotalWastedTimeMicroSec() const;            // Get the total time all units wasted while processing this job list.
        uint64 GetUnitProcessingTimeMicroSec(int unit) const; // Time the given unit spent processing this job list.
        uint64 GetUnitWastedTimeMicroSec(int unit) const;     // Time the given unit wasted while processing this job list.

        uint32 mNumExecutedJobs;
        uint32 mNumExecutedSyncs;
        uint64 mSubmitTime;
        uint64 mStartTime;
        uint64 mEndTime;
        uint64 mWaitTime;
        uint64 mThreadExecTime[CONFIG_MAX_THREADS];
        uint64 mThreadTotalTime[CONFIG_MAX_THREADS];
    };

    class JobList
    {
        friend class JobManagerLocal;
        friend class JobListInstance;

    public:
        void AddJob(JobRun_t function, void* data);
        void InsertSyncPoint(EJobSyncType_t syncType);

        // Submit the jobs in this list.
        void Submit(JobList* waitForJobList = nullptr, int32 parallelism = JOBLIST_PARALLELISM_DEFAULT);
        void Wait();              // Wait for the jobs in this list to finish. Will spin in place if any jobs are not done.
        bool TryWait();           // Try to wait for the jobs in this list to finish but either way return immediately. Returns true if all jobs are done.
        bool IsSubmitted() const; // returns true if the job list has been submitted.

        JobListId_t          GetId() const;                      // Get the job list ID
        const char*          GetName() const { return mName; }   // Get the job list name
        const uint32         GetColor() const { return mColor; } // Get the color for profiling.
        ThreadStats_t const* GetStats() const;                   // Get the stats for this job list.

    protected:
        JobListInstance* mJobListInstance;
        const char*      mName;
        const uint32     mColor;

        JobList(Alloc*, JobListId_t id, const char* name, EJobListPriority_t priority, uint32 maxJobs, uint32 maxSyncs, const uint32 color);
        ~JobList();
    };

    class JobsManager
    {
    public:
        virtual ~JobsManager() {}

        virtual void Init(Alloc* allocator, int32 jobs_numThreads) = 0;
        virtual void Shutdown()                  = 0;

        virtual JobList* AllocJobList(const char* name, EJobListPriority_t priority, uint32 maxJobs, uint32 maxSyncs, const uint32 color) = 0;
        virtual void     FreeJobList(JobList* jobList)                                                                                    = 0;

        virtual int      GetNumJobLists() const     = 0;
        virtual int      GetNumFreeJobLists() const = 0;
        virtual JobList* GetJobList(int index)      = 0;

        virtual int  GetNumProcessingUnits() = 0;
        virtual void WaitForAllJobLists()    = 0;

        virtual bool        IsRegisteredJob(JobRun_t function) const         = 0;
        virtual void        RegisterJob(JobRun_t function, const char* name) = 0;
        virtual const char* GetJobName(JobRun_t function) const              = 0;
    };

    extern JobsManager* g_JobManager;

} // namespace njobs

#endif // __CJOBS_H__