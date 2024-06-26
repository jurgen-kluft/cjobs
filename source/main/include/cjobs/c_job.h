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
        struct system_t;

        // -----------------------------------------------------------------------------------------------------------------------
        // Job

        class job_t
        {
        public:
            virtual ~job_t() {}
            virtual void execute(s32 from, s32 to) = 0;
        };

        // Note: As a user you should take care of a job handle, if you do not plan to use it anymore, you should release it.
        typedef void* job_handle_t;

        // -----------------------------------------------------------------------------------------------------------------------
        void g_create(alloc_t* allocator, system_t*& system, s32 threadCount = 1);

        // -----------------------------------------------------------------------------------------------------------------------
        void g_run(system_t* system, job_t* job, job_handle_t dependsOn = nullptr);
        void g_run(system_t* system, job_t* job, s32 totalIterCount, job_handle_t dependsOn = nullptr);
        void g_run(system_t* system, job_t* job, s32 totalIterCount, s32 innerIterCount, job_handle_t dependsOn = nullptr);

        // -----------------------------------------------------------------------------------------------------------------------
        job_handle_t g_schedule_single(system_t* system, job_t* job, job_handle_t dependsOn = nullptr);
        job_handle_t g_schedule_single(system_t* system, job_t* job, s32 totalIterCount, job_handle_t dependsOn = nullptr);
        job_handle_t g_schedule_parallel(system_t* system, job_t* job, s32 totalIterCount, s32 innerIterCount, job_handle_t dependsOn = nullptr);

        // Job handle will be set to nullptr and the job will be released or marked as to be released
        bool g_releaseHandle(system_t* system, job_handle_t& handle);

        // Job handle will be set to nullptr when the job is done
        // Note: Should we help with executing some work ?
        void g_waitUntilDone(system_t* system, job_handle_t& handle);
        void g_waitUntilDoneWithTimeOut(system_t* system, job_handle_t& handle, u32 milliseconds);

    } // namespace njob

} // namespace ncore

#endif // __CJOBS_JOB_H__
