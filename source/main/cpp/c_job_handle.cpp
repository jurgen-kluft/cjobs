#include "ccore/c_target.h"
#include "cbase/c_allocator.h"
#include "cjobs/c_queue.h"
#include "cjobs/c_job.h"

namespace ncore
{
    namespace njob
    {
        struct job_handle_internal_t
        {
            u64  jobGroup;
            s32  jobIndex;
            int  debugVersion;
            s32* debugInfo;

            void Complete()
            {
                if (jobGroup == 0)
                    return;

                ScheduleBatchedJobsAndComplete(this);
            }
            bool IsCompleted() { return ScheduleBatchedJobsAndIsCompleted(this); }

            static void CompleteAll(job_handle_internal_t*& job0, job_handle_internal_t*& job1)
            {
                job_handle_internal_t* jobs[2];
                jobs[0] = job0;
                jobs[1] = job1;
                ScheduleBatchedJobsAndCompleteAll(jobs, 2);
                job0 = nullptr;
                job1 = nullptr;
            }

            static void CompleteAll(job_handle_internal_t*& job0, job_handle_internal_t*& job1, job_handle_internal_t*& job2)
            {
                job_handle_internal_t* jobs[3];
                jobs[0] = job0;
                jobs[1] = job1;
                jobs[2] = job2;
                ScheduleBatchedJobsAndCompleteAll(jobs, 3);
                job0 = nullptr;
                job1 = nullptr;
                job2 = nullptr;
            }

            static void CompleteAll(job_handle_internal_t** jobs, int count) { ScheduleBatchedJobsAndCompleteAll(jobs, count); }

            static void ScheduleBatchedJobs();
            static void ScheduleBatchedJobsAndComplete(job_handle_internal_t* job);
            static bool ScheduleBatchedJobsAndIsCompleted(job_handle_internal_t* job);
            static void ScheduleBatchedJobsAndCompleteAll(job_handle_internal_t** jobs, int count);

            static job_handle_internal_t CombineDependencies(job_handle_internal_t* job0, job_handle_internal_t* job1) { return CombineDependenciesInternal2(job0, job1); }
            static job_handle_internal_t CombineDependencies(job_handle_internal_t* job0, job_handle_internal_t* job1, job_handle_internal_t job2) { return CombineDependenciesInternal3(job0, job1, job2); }
            static job_handle_internal_t CombineDependencies(job_handle_internal_t** jobs, int count) { return CombineDependenciesInternalPtr(jobs, count); }
            static job_handle_internal_t CombineDependencies(job_handle_internal_t** jobs, int count) { return CombineDependenciesInternalPtr(jobs, count); }

            static job_handle_internal_t CombineDependenciesInternal2(job_handle_internal_t* job0, job_handle_internal_t* job1);
            static job_handle_internal_t CombineDependenciesInternal3(job_handle_internal_t* job0, job_handle_internal_t* job1, job_handle_internal_t* job2);
            static job_handle_internal_t CombineDependenciesInternalPtr(job_handle_internal_t** jobs, int count);
            static bool                  CheckFenceIsDependencyOrDidSyncFence(job_handle_internal_t* job, job_handle_internal_t* dependsOn);
        };

    } // namespace njob
} // namespace ncore
