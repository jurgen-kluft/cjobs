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
        typedef void* job_handle_t;
        class system_t;

        // -----------------------------------------------------------------------------------------------------------------------
        // Job (Single/Run)

        class ijob
        {
        public:
            virtual ~ijob() {}
            virtual void execute() = 0;
        };

        job_handle_t g_schedule(system_t* system, ijob* job, job_handle_t dependsOn = nullptr);
        void         g_run(ijob* job);

        // -----------------------------------------------------------------------------------------------------------------------
        // Job For (Parallel)

        class ijob_for
        {
        public:
            virtual ~ijob_for() {}
            virtual void execute(s32 index) = 0;
        };

        job_handle_t g_schedule(system_t* system, ijob_for* job, s32 arrayLength, job_handle_t dependsOn = nullptr);
        job_handle_t g_schedule_parallel(system_t* system, ijob_for* job, s32 arrayLength, s32 innerloopBatchCount, job_handle_t dependsOn = nullptr);

        job_handle_t g_run(system_t* system, ijob_for* job, s32 arrayLength);

        // -----------------------------------------------------------------------------------------------------------------------
        // Job Parallel For

        class ijob_parallel_for
        {
        public:
            virtual ~ijob_parallel_for() {}
            virtual void execute(s32 index) = 0;
        };

        job_handle_t g_schedule(system_t* system, ijob_parallel_for* job, s32 arrayLength, s32 innerloopBatchCount, job_handle_t dependsOn = nullptr);
        job_handle_t g_run(system_t* system, ijob_parallel_for* job, s32 arrayLength);

    } // namespace njobs

} // namespace ncore

#endif // __CJOBS_JOB_H__
