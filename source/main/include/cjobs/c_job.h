#ifndef __CJOBS_JOB_H__
#define __CJOBS_JOB_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    class alloc_t;

    namespace njob
    {
        class job_t;

        // -----------------------------------------------------------------------------------------------------------------------
        // Job Graph, this needs to be setup before any job is scheduled, however, jobs by themselves can schedule any new jobs
        // that are not part of the graph. To deal with dependencies in those new jobs, the user can use the 'job_t::job_finished()'
        // callback to schedule more jobs.
        struct system_t;
        struct graph_t;

        graph_t* g_createGraph(alloc_t* allocator, system_t* system, s32 maxJobs, s32 maxDependencies, s32 maxGroups);
        void     g_destroyGraph(alloc_t* allocator, graph_t*& graph);

        void graph_reset(graph_t* graph);
        void graph_push_group(graph_t* graph, const char* name);
        void graph_pop_group(graph_t* graph);
        void graph_add_job(graph_t* graph, job_t* job);
        void graph_add_job(graph_t* graph, job_t* job, s32 totalIterCount, s32 innerIterCount);
        void graph_execute(graph_t* graph);

        // -----------------------------------------------------------------------------------------------------------------------
        // Job, a job is a unit of work that can be scheduled to run on a system. Jobs can be scheduled to run in parallel or
        // just to run on one thread.

        class job_t
        {
        public:
            virtual ~job_t() {}
            virtual const char* job_name() const                                   = 0; // For debugging and profiling
            virtual void        job_execute(s32 from, s32 to)                      = 0; //
            virtual s32         job_finished(job_t** job_array, s32 job_array_max) = 0; // Returns the number of jobs to schedule if any
        };

        // -----------------------------------------------------------------------------------------------------------------------
        void g_create(alloc_t* allocator, system_t*& system, s32 threadCount = 4);
        void g_destroy(alloc_t* allocator, system_t*& system);

        // -----------------------------------------------------------------------------------------------------------------------
        void g_schedule(system_t* system, u64 current_thread_id, job_t* job);
        void g_schedule_single(system_t* system, u64 current_thread_id, job_t* job, s32 totalIterCount);
        void g_schedule_parallel(system_t* system, u64 current_thread_id, job_t* job, s32 totalIterCount, s32 innerIterCount);

    } // namespace njob

} // namespace ncore

#endif // __CJOBS_JOB_H__
