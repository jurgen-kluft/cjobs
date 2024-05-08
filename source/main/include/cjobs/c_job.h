#ifndef __CJOBS_JOB_H__
#define __CJOBS_JOB_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    class alloc_t;

    namespace njobs
    {
        // Job (Single/Run)

        class ijob
        {
        public:
            virtual ~ijob() {}
            virtual void execute() = 0;
        };

        typedef void* job_handle_t;

        job_handle_t g_schedule(ijob* job, job_handle_t dependsOn = nullptr);
        void         g_run(ijob* job);

        // Job For (Parallel)

        class ijob_for
        {
        public:
            virtual ~ijob_for() {}
            virtual void execute(s32 index) = 0;
        };

        job_handle_t g_schedule(ijob_for* job, s32 arrayLength, job_handle_t dependsOn = nullptr);
        job_handle_t g_schedule_parallel(ijob_for* job, s32 arrayLength, s32 innerloopBatchCount, job_handle_t dependsOn = nullptr);

        job_handle_t g_run(ijob_for* job, s32 arrayLength);

        // Job Parallel For

        class ijob_parallel_for
        {
        public:
            virtual ~ijob_parallel_for() {}
            virtual void execute(s32 index) = 0;
        };

        job_handle_t g_schedule(ijob_parallel_for* job, s32 arrayLength, s32 innerloopBatchCount, job_handle_t dependsOn = nullptr);
        job_handle_t g_run(ijob_parallel_for* job, s32 arrayLength);

    } // namespace njobs

} // namespace ncore

#endif // __CJOBS_JOB_H__
