#include "ccore/c_target.h"
#include "cbase/c_allocator.h"
#include "cbase/c_integer.h"

#include "cjobs/c_queue.h"
#include "cjobs/c_job.h"
#include "cjobs/private/c_signal.h"

#include <atomic>

#ifdef TARGET_PC
#    include <windows.h>
#endif

#ifdef TARGET_MAC
#    include <mach/mach.h>
#    include <mach/task.h>
#endif

namespace ncore
{
    namespace njob
    {
        struct work_t                            // 64 bytes
        {                                        //
            job_t*           m_job;              // Job to execute (user)
            s32              m_total_iter_count; // Total loop iteration count
            s16              m_inner_iter_count; // Inner loop iteration count
            s16              m_worker_index;     // Worker thread that created this job
            work_t*          m_dependency;       // Job that needs to be done before this job can be executed
            signal_t*        m_signal;           // Signal to wait on when this job is not done
            std::atomic<s32> m_index;            // Index, begin and end can be calculated from job->m_total_iter_count and job->m_inner_iter_count
            s32              m_dummy;            // Padding to make sure that this struct is cacheline aligned
            void*            m_dummy3[3];        // Padding to make sure that this struct is cacheline aligned
        };

        struct system_t
        {
            u32               m_worker_thread_count; // Number of worker threads
            worker_ctx_t*     m_worker_thread_ctxs;  // Worker thread contexts, worker_ctx_t[m_max_workers]
            std::atomic<bool> m_quit;
        };

        struct worker_ctx_t
        {
            s32                    m_worker_index;    //
            u32                    m_max_jobs;        // Maximum number of jobs that can be created by this worker
            work_t*                m_jobs;            // Array of jobs, work_t[m_max_jobs]
            local_queue_t*         m_jobs_free_queue; // This worker can take a new job from this queue and initialize it
            local_queue_t*         m_jobs_new_queue;  // Queue of jobs that need to be processed
            mpsc_queue_t*          m_jobs_done_queue; // These are jobs that are 'done', can be pushed here from any worker thread
            mpsc_blocking_queue_t* m_scheduled_work;  // Worker thread work queue
            signal_t*              m_signal;          // Signal this worker thread is waiting on when there is no work
            counter_t*             m_jobs_count;      // Number of jobs that are not done
        };

        struct worker_t
        {
            system_t*     m_main_ctx;
            worker_ctx_t* m_worker_ctx;
            s32           m_worker_index;
        };

        void g_create(alloc_t* allocator, system_t*& system, s32 threadCount)
        {
            u32 const max_jobs = 1024;

            system                        = (system_t*)allocator->allocate(sizeof(system_t));
            system->m_worker_thread_count = threadCount;
            system->m_worker_thread_ctxs  = (worker_ctx_t*)allocator->allocate(threadCount * sizeof(worker_ctx_t));
            system->m_quit.store(false);

            // Create worker threads
            for (s32 i = 0; i < threadCount; ++i)
            {
                worker_ctx_t* ctx      = system->m_worker_thread_ctxs + i;
                ctx->m_worker_index    = i;
                ctx->m_max_jobs        = max_jobs;
                ctx->m_jobs            = (work_t*)allocator->allocate(ctx->m_max_jobs * sizeof(work_t));
                ctx->m_jobs_free_queue = local_queue_create(allocator, ctx->m_max_jobs);
                ctx->m_jobs_new_queue  = local_queue_create(allocator, ctx->m_max_jobs);
                ctx->m_jobs_done_queue = mpsc_queue_create(allocator, ctx->m_max_jobs);
                ctx->m_scheduled_work  = mpsc_blocking_queue_create(allocator, ctx->m_max_jobs);
            }

            // Start worker threads
            for (s32 i = 0; i < threadCount; ++i)
            {
                worker_t* worker       = (worker_t*)allocator->allocate(sizeof(worker_t));
                worker->m_main_ctx     = system;
                worker->m_worker_ctx   = system->m_worker_thread_ctxs + i;
                worker->m_worker_index = i;
                // thread_create(worker_thread, worker);
            }
        }

        inline static void s_job_create(work_t* job, alloc_t* allocator) { signal_create(allocator, job->m_signal); }
        inline static void s_job_destroy(work_t* job, alloc_t* allocator) { signal_destroy(allocator, job->m_signal); }
        inline static void s_job_reset(work_t* job, s32 N) { signal_reset(job->m_signal); }
        inline static bool s_job_set_done(work_t* job) { return signal_set(job->m_signal); }
        inline static void s_job_wait_until_done(work_t* job) { signal_wait(job->m_signal); }

        // A job will be scheduled as one or many work items depending on how the user wants to schedule it
        static job_handle_t s_schedule_single(worker_t* worker, job_t* job)
        {
            system_t*     main_ctx   = worker->m_main_ctx;
            worker_ctx_t* worker_ctx = worker->m_worker_ctx;

            u64 job_index = 0;
            queue_dequeue(worker_ctx->m_jobs_free_queue, job_index);
            work_t* job_item             = worker_ctx->m_jobs + job_index;
            job_item->m_job              = job;
            job_item->m_total_iter_count = 1;
            job_item->m_inner_iter_count = 1;
            job_item->m_dependency       = nullptr;
            s_job_reset(job_item, 1);

            // Should we enqueue this job on all the worker queues ?
            // The top u32 of the job_index should be the worker thread index that created this job,
            // the bottom 32 bits should be the job index.
            job_index = (worker_ctx->m_worker_index << 32) | (job_index & 0xFFFFFFFF);
            queue_enqueue(worker_ctx->m_scheduled_work, job_index);

            // TODO
            // If we have idle worker threads, signal one of them.
            // When there are no idle worker threads, signal all worker threads.

            return (job_handle_t)job_item;
        }

        static job_handle_t s_schedule_for(worker_t* worker, job_t* job, s32 array_length)
        {
            system_t*     main_ctx   = worker->m_main_ctx;
            worker_ctx_t* worker_ctx = worker->m_worker_ctx;

            u64 job_index = 0;
            queue_dequeue(worker_ctx->m_jobs_free_queue, job_index);
            work_t* job_item             = worker_ctx->m_jobs + job_index;
            job_item->m_job              = job;
            job_item->m_total_iter_count = 1;
            job_item->m_inner_iter_count = 1;
            job_item->m_dependency       = nullptr;
            s_job_reset(job_item, 1);

            // Should we enqueue this job on all the worker queues ?
            // The top u32 of the job_index should be the worker thread index that created this job
            job_index = (worker_ctx->m_worker_index << 32) | (job_index & 0xFFFFFFFF);
            queue_enqueue(worker_ctx->m_scheduled_work, job_index);

            // TODO
            // If we have idle worker threads, signal one of them.
            // When there are no idle worker threads, signal all worker threads.

            return (job_handle_t)job_item;
        }

        static job_handle_t s_schedule_parallel(worker_t* worker, job_t* job, s32 array_length, s32 inner_count)
        {
            system_t*     main_ctx   = worker->m_main_ctx;
            worker_ctx_t* worker_ctx = worker->m_worker_ctx;

            u64 job_index = 0;
            queue_dequeue(worker_ctx->m_jobs_free_queue, job_index);
            work_t* job_item             = worker_ctx->m_jobs + job_index;
            job_item->m_job              = job;
            job_item->m_total_iter_count = 1;
            job_item->m_inner_iter_count = 1;
            job_item->m_dependency       = nullptr;

            // The work is divided into N work items, for every finished work item
            // the worker thread will decrement the count of the job, when the count
            // reaches zero the job is done.
            s32 const N = (array_length + inner_count - 1) / inner_count;
            s_job_reset(job_item, N);

            // Should we enqueue this job on all the worker queues ?
            // The top u32 of the job_index should be the worker thread index that created this job.
            // The bottom 32 bits should be the job index.
            job_index = (worker_ctx->m_worker_index << 32) | (job_index & 0xFFFFFFFF);
            queue_enqueue(worker_ctx->m_scheduled_work, job_index);

            // TODO
            // Signal all the worker threads that there is work to do
            for (s32 i = 0; i < main_ctx->m_worker_thread_count; ++i)
            {
                // semaphore_signal(main_ctx->m_worker_thread_ctxs[i].m_semaphore);
            }

            return (job_handle_t)job_item;
        }

        static void s_worker_thread(worker_t* worker)
        {
            system_t*     main_ctx     = worker->m_main_ctx;
            worker_ctx_t* this_ctx     = worker->m_worker_ctx;
            s32 const     worker_index = worker->m_worker_index;

            while (!main_ctx->m_quit.load())
            {
                // NOTE: we could be taking all work out of the queue and possibly even
                //       sort it by priority

                u64 work;
                s32 const queued_items = queue_dequeue(this_ctx->m_scheduled_work, work);
                if (!)
                {
                    // Here we are unblocked, so we can go at it again to try and acquire work
                    continue;
                }

                // Get the job object
                // Top part of the u64 is the worker index that created the job
                u32 const job_index   = work & 0xFFFFFFFF;
                u32 const owner_index = (work >> 32) & 0xFFFFFFFF;
                work_t*   job         = main_ctx->m_worker_thread_ctxs[owner_index].m_jobs + job_index;

                // Keep working this job until it is done
                while (true)
                {
                    s32 const work_index = job->m_index.fetch_add(1);

                    // Compute the begin and end of the work item
                    s32 const work_range = job->m_total_iter_count / job->m_inner_iter_count;
                    s32 const work_begin = math::min(work_index * work_range, job->m_total_iter_count);
                    s32 const work_end   = math::min(work_begin + work_range, job->m_total_iter_count);

                    // Make sure that this index is within range
                    if (work_begin < work_end)
                    {
                        job->m_job->execute(work_begin, work_end); // Execute this work range
                    }
                    else
                    {
                        if (s_job_set_done(job)) // Mark the job as done
                        {
                            // We where the first to mark this job as done, so we can push it to the done queue
                            // of the worker thread that created this job (owner_index)
                            queue_enqueue(main_ctx->m_worker_thread_ctxs[owner_index].m_jobs_done_queue, job_index);
                        }
                        break; // This job is done, move on to the next job
                    }
                }
            }
        }

        // Example of granularity being too fine:
        //    array_length = 10000, inner_count = 10, this means we will schedule 1000 work items.
        //    If we have 4 worker threads, then each worker thread will have 250 work items to process.
        //    Furthermore the amount of stealing will be high which will lead to a lot of contention.
        // Correction to the example above:
        //    array_length = 10000, inner_count = 100, this means we will schedule 100 work items.
        //    If we have 4 worker threads, then each worker thread will have 25 work items to process. Now
        //    work items take longer to process and the amount of stealing will be lower which will lead to
        //    less contention.

        // How to handle dependencies and wait ?
        // What about sync points, perhaps this is just a special empty job ?

        // Job handle can point directly to work_t

    } // namespace njob
} // namespace ncore
