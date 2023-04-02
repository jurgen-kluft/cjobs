#ifndef __CJOBS_H__
#define __CJOBS_H__

#include "cjobs/private/c_sys.h"

namespace cjobs
{
    typedef short              int16;
    typedef unsigned short     uint16;
    typedef int                int32;
    typedef unsigned int       uint32;
    typedef long long          int64;
    typedef unsigned long long uint64;

    enum EAllocTags
    {
        TAG_JOBTHREAD  = 0x4a4f4254, // "JOBT"
        TAG_JOBLIST    = 0x4a4c4f42, // "JOBL"
        TAG_JOBMANAGER = 0x4a4d4e47  // "JMNG"
    };

    class Alloc
    {
    public:
        void* Allocate(uint64 size, int32 alignment = sizeof(void*), int32 tag = 0) { return v_Allocate(size, alignment, tag); }
        void  Deallocate(void* ptr) { v_Deallocate(ptr); }

    protected:
        virtual void* v_Allocate(uint64 size, int32 alignment, int32 tag = 0) = 0;
        virtual void  v_Deallocate(void* ptr)                                 = 0;
    };

    typedef void (*JobRun_t)(void*);

    enum EJobsSyncType_t
    {
        JOBSSYNC_NONE,
        JOBSSYNC_SIGNAL,
        JOBSSYNC_SYNCHRONIZE
    };

    typedef int32 JobsListId_t;

    enum EJobsListPriority_t
    {
        JOBSLIST_PRIORITY_NONE,
        JOBSLIST_PRIORITY_LOW,
        JOBSLIST_PRIORITY_MEDIUM,
        JOBSLIST_PRIORITY_HIGH
    };

    enum EJobsListParallelism_t
    {
        JOBSLIST_PARALLELISM_NONE    = 0,                        // run on the current (calling) thread
        JOBSLIST_PARALLELISM_MAX     = -1,                       // use the maximum number of job threads, which can help if there is IO to overlap
        JOBSLIST_PARALLELISM_DEFAULT = JOBSLIST_PARALLELISM_MAX, // use "jobs_numThreads" number of threads
    };

    enum EJobsManagerConfig
    {
        CONFIG_MAX_REGISTERED_JOBS = 256,
        CONFIG_MAX_THREADS         = 16,
        CONFIG_MAX_JOBTHREADS      = 16,
        CONFIG_MAX_JOBLISTS        = 32,
    };

    enum EJobsColor // RGBA
    {
        COLOR_RED    = 0xff0000ff,
        COLOR_GREEN  = 0x00ff00ff,
        COLOR_BLUE   = 0x0000ffff,
        COLOR_WHITE  = 0xffffffff,
        COLOR_BLACK  = 0x000000ff,
        COLOR_GRAY   = 0x808080ff,
        COLOR_CYAN   = 0x00ffffff,
        COLOR_YELLOW = 0xffff00ff,
        COLOR_PURPLE = 0xff00ffff,
        COLOR_ORANGE = 0xffa500ff,
        COLOR_BROWN  = 0xa52a2aff,
        COLOR_PINK   = 0xffc0cbff,
    };

    struct ThreadStats_t
    {
        uint32 GetNumExecutedJobs() const;                      // Get the number of jobs executed in this job list.
        uint32 GetNumSyncs() const;                             // Get the number of sync points.
        uint64 GetSubmitTimeMicroSec() const;                   // Time at which the job list was submitted.
        uint64 GetStartTimeMicroSec() const;                    // Time at which execution of this job list started.
        uint64 GetFinishTimeMicroSec() const;                   // Time at which all jobs in the list were executed.
        uint64 GetWaitTimeMicroSec() const;                     // Time the host thread waited for this job list to finish.
        uint64 GetTotalProcessingTimeMicroSec() const;          // Get the total time all units spent processing this job list.
        uint64 GetTotalWastedTimeMicroSec() const;              // Get the total time all units wasted while processing this job list.
        uint64 GetUnitProcessingTimeMicroSec(int32 unit) const; // Time the given unit spent processing this job list.
        uint64 GetUnitWastedTimeMicroSec(int32 unit) const;     // Time the given unit wasted while processing this job list.

        uint32 mNumExecutedJobs;
        uint32 mNumExecutedSyncs;
        uint64 mSubmitTime;
        uint64 mStartTime;
        uint64 mEndTime;
        uint64 mWaitTime;
        uint64 mThreadExecTime[CONFIG_MAX_THREADS];
        uint64 mThreadTotalTime[CONFIG_MAX_THREADS];
    };

    class JobsListInstance;

    class JobsList
    {
    public:
        JobsList(Alloc* allocator, JobsListId_t id, JobsListDescr const& descr);
        ~JobsList();

        void AddJob(JobRun_t function, void* data);
        void InsertSyncPoint(EJobsSyncType_t syncType);

        // Submit the jobs in this list.
        void Submit(JobsList* waitForJobList = nullptr, int32 parallelism = JOBSLIST_PARALLELISM_DEFAULT);
        void Wait();              // Wait for the jobs in this list to finish. Will spin in place if any jobs are not done.
        bool TryWait();           // Try to wait for the jobs in this list to finish but either way return immediately. Returns true if all jobs are done.
        bool IsSubmitted() const; // returns true if the job list has been submitted.

        JobsListId_t         GetId() const;    // Get the job list ID
        const char*          GetName() const;  // Get the job list name
        uint32               GetColor() const; // Get the color for profiling.
        ThreadStats_t const* GetStats() const; // Get the stats for this job list.

    protected:
        JobsListInstance* mJobsListInstance;
    };

    struct JobsThreadDescr
    {
        JobsThreadDescr(const char* name, int32 core, int32 stackSize)
            : Name(name)
            , Core(core)
            , StackSize(stackSize)
        {
        }
        const char* const Name;
        const int32       Core;
        const int32       StackSize;
    };

    struct JobsListDescr
    {
        JobsListDescr(const char* name, EJobsListPriority_t priority, int16 maxJobs, int16 maxSyncs, const uint32 color)
            : Name(name)
            , Priority(priority)
            , MaxJobs(maxJobs)
            , MaxSyncs(maxSyncs)
            , Color(color)
        {
        }
        const char* const         Name;
        const EJobsListPriority_t Priority;
        const int16               MaxJobs;
        const int16               MaxSyncs;
        const uint32              Color;
    };

    class JobsManager
    {
    public:
        virtual ~JobsManager() {}

        virtual void Init(JobsThreadDescr threads[], int32 num) = 0;
        virtual void Shutdown()                                 = 0;

        virtual JobsList* AllocJobList(JobsListDescr const& descr) = 0;
        virtual void      FreeJobList(JobsList* jobList)           = 0;

        virtual int32     GetNumJobLists() const     = 0;
        virtual int32     GetNumFreeJobLists() const = 0;
        virtual JobsList* GetJobList(int32 index)    = 0;

        virtual int32 GetNumProcessingUnits() const = 0;
        virtual void  WaitForAllJobLists()          = 0;

        virtual bool        IsRegisteredJob(JobRun_t function) const         = 0;
        virtual void        RegisterJob(JobRun_t function, const char* name) = 0;
        virtual const char* GetJobName(JobRun_t function) const              = 0;
    };

    JobsManager* CreateJobManager(Alloc* allocator);
    void         DestroyJobManager(JobsManager* manager);

} // namespace cjobs

#endif // __CJOBS_H__
