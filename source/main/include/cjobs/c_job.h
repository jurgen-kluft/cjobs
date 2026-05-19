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

        graph_t* g_create_graph(alloc_t* allocator, system_t* system, s32 maxJobs, s32 maxGroups);
        void     g_destroy(alloc_t* allocator, graph_t*& graph);

        void graph_reset(graph_t* graph);
        void graph_push_group(graph_t* graph, const char* name);
        void graph_pop_group(graph_t* graph);
        void graph_add_job(graph_t* graph, job_t* job);
        void graph_add_job(graph_t* graph, job_t* job, s32 totalIterCount, s32 innerIterCount);
        void graph_execute(graph_t* graph);

        // -----------------------------------------------------------------------------------------------------------------------
        // Job, a job is a unit of work that can be scheduled to run on a system. Jobs can be scheduled to run in parallel or
        // just to run on one thread.
        typedef const char* (*job_name_fn)(void* user);                                   // For debugging and profiling
        typedef void (*job_execute_fn)(void* user, s32 from, s32 to);                     // The 'from' and 'to' parameters are used to specify the range of iterations for parallel jobs, for non-parallel jobs, 'from' will be 0 and 'to' will be 1
        typedef s32 (*job_finished_fn)(void* user, job_t** job_array, s32 job_array_max); // Returns the number of jobs to schedule if any

        struct job_t
        {
            void*           m_user;        // User data that is passed to the job functions
            job_name_fn     m_name_fn;     // (optional) Function to get the name of the job, for debugging and profiling
            job_execute_fn  m_execute_fn;  // Function to execute the job
            job_finished_fn m_finished_fn; // Function that is called when the job is finished, this is used to schedule more jobs if needed, for example, if a job is a 'for
        };

        // -----------------------------------------------------------------------------------------------------------------------
        void g_create_system(alloc_t* allocator, system_t*& system, s32 num_workers = 4, s32 max_running_threads = 16);
        void g_destroy(alloc_t* allocator, system_t*& system);

        // -----------------------------------------------------------------------------------------------------------------------
        void g_schedule(system_t* system, u64 current_thread_id, job_t* job);
        void g_schedule_single(system_t* system, u64 current_thread_id, job_t* job, s32 totalIterCount);
        void g_schedule_parallel(system_t* system, u64 current_thread_id, job_t* job, s32 totalIterCount, s32 innerIterCount);

    } // namespace njob

} // namespace ncore

#endif // __CJOBS_JOB_H__
