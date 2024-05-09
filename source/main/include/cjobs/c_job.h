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
        class system_t;

        // -----------------------------------------------------------------------------------------------------------------------
        // Job

        class ijob_t
        {
        public:
            virtual ~ijob_t() {}
            virtual void execute(s32 index) = 0;
        };

        // Note: As a user you should take care of a job handle, if you do not plan to use it anymore, you should release it.
        typedef void* job_handle_t; 
        
        // -----------------------------------------------------------------------------------------------------------------------
        void g_run(system_t* system, ijob_t* job, job_handle_t dependsOn = nullptr);
        void g_run(system_t* system, ijob_t* job, s32 arrayLength, job_handle_t dependsOn = nullptr);
        void g_run(system_t* system, ijob_t* job, s32 arrayLength, s32 innerloopBatchCount, job_handle_t dependsOn = nullptr);

        // -----------------------------------------------------------------------------------------------------------------------
        job_handle_t g_schedule_single(system_t* system, ijob_t* job, job_handle_t dependsOn = nullptr);
        job_handle_t g_schedule_single(system_t* system, ijob_t* job, s32 arrayLength, job_handle_t dependsOn = nullptr);
        job_handle_t g_schedule_parallel(system_t* system, ijob_t* job, s32 arrayLength, s32 innerloopBatchCount, job_handle_t dependsOn = nullptr);

        // Job handle will be set to nullptr and the job will be released or marked as to be released
        bool g_releaseHandle(system_t* system, job_handle_t& handle);

        // Job handle will be set to nullptr when the job is done
        // Note: Should we help with executing some work ?
        void g_waitUntilDone(system_t* system, job_handle_t& handle);
        void g_waitUntilDoneWithTimeOut(system_t* system, job_handle_t& handle, u32 milliseconds);

    } // namespace njob

} // namespace ncore

#endif // __CJOBS_JOB_H__
