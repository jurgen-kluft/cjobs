#include "ccore/c_target.h"
#include "cbase/c_allocator.h"
#include "cbase/c_integer.h"

#include "cjobs/c_queue.h"
#include "cjobs/c_job.h"

#include <atomic>

#ifdef TARGET_PC
#    include <windows.h>
#endif

#ifdef TARGET_MAC
#    include <mach/mach.h>
#    include <mach/task.h>
#    include <mach/semaphore.h>
#endif

namespace ncore
{
    namespace njob
    {
        // Use std::thread
        // https://en.cppreference.com/w/cpp/thread

        enum EScheduleMode
        {
            Single   = 0,
            Parallel = 1,
            Run      = 2,
        };

        enum EJobType
        {
            Single      = 0,
            ParallelFor = 1
        };

#ifdef TARGET_MAC
        struct job_done_t
        {
            std::atomic<s32> m_done_count;
            s32              m_total_count;
            semaphore_t      m_done;
            inline void      init(s32 count)
            {
                semaphore_create(mach_task_self(), &m_done, SYNC_POLICY_FIFO, count);
                m_done_count.store(0);
            }
            inline bool done()
            {
                s32 const done = m_done_count.fetch_add(1);
                semaphore_signal(m_done);
                return done == m_total_count - 1;
            }
            inline void exit() { semaphore_destroy(mach_task_self(), m_done); }
            inline bool is_done() const { return m_done_count.load() == m_total_count; }
            inline void signal() { semaphore_signal(m_done); }
            inline void wait() { semaphore_wait(m_done); }
            bool        try_wait(u32 milliseconds)
            {
                mach_timespec_t ts;
                ts.tv_sec  = milliseconds / 1000;
                ts.tv_nsec = (milliseconds % 1000) * 1000000;
                return semaphore_timedwait(m_done, ts) == KERN_SUCCESS;
            }
        };
#elif defined(TARGET_PC)
        struct job_done_t
        {
            std::atomic<s32> m_done_count;
            s32              m_total_count;
            void*            m_done;
            inline void      init(s32 count)
            {
                // semaphore_create(mach_task_self(), &m_done, SYNC_POLICY_FIFO, count);
                m_done = CreateSemaphoreW(nullptr, 0, count, nullptr);
                m_done_count.store(0);
            }
            inline bool done()
            {
                s32 const done = m_done_count.fetch_add(1);
                // semaphore_signal(m_done);
                signal();
                return done == m_total_count - 1;
            }
            inline void exit()
            {
                // semaphore_destroy(mach_task_self(), m_done);
                CloseHandle(m_done);
            }
            inline bool is_done() const { return m_done_count.load() == m_total_count; }
            inline void signal()
            {
                // semaphore_signal(m_done);
                if (!ReleaseSemaphore(m_done, 1, nullptr))
                {
                    // cannot signal semaphore
                }
            }
            inline void wait()
            {
                // semaphore_wait(m_done);
                switch (WaitForSingleObject(m_done, INFINITE))
                {
                    case WAIT_OBJECT_0: return;
                    default:
                        // wait for semaphore failed
                        break;
                }
            }
            bool try_wait(u32 milliseconds)
            {
                switch (WaitForSingleObject(m_done, milliseconds))
                {
                    case WAIT_OBJECT_0: return true;
                    default:
                        // wait for semaphore failed
                        break;
                }
                return false;
            }
        };
#endif

        struct job_t
        {
            ijob_t*          m_job;
            s32              m_array_length;
            s16              m_inner_count;
            s16              m_worker_index; // Worker thread that created this job
            job_handle_t*    m_dependency;
            job_done_t       m_done;
            std::atomic<s32> m_index; // Index, begin and end can be calculated from job->m_array_length and job->m_inner_count
        };

        struct main_ctx_t;

        struct worker_thread_ctx_t
        {
            main_ctx_t* m_main_ctx;
            s32         m_worker_thread_index;
            // semaphore_t m_semaphore; // Semaphore to signal this worker thread

            u32            m_max_jobs;        // Maximum number of jobs that can be created by this worker
            job_t*         m_jobs;            // Array of jobs, job_t[m_max_jobs]
            local_queue_t* m_jobs_free_queue; // This worker can take a new job from this queue and initialize it
            local_queue_t* m_jobs_new_queue;  // Queue of jobs that need to be processed
            mpsc_queue_t*  m_jobs_done_queue; // These are jobs that are 'done', can be pushed here from any worker thread
            spmc_queue_t*  m_scheduled_work;  // Worker thread work queue
        };

        struct main_ctx_t
        {
            u32                  m_max_worker_threads;  // Number of worker threads
            mpmc_queue_t*        m_idle_worker_threads; // Worker threads that have no work, queue<s32>
            worker_thread_ctx_t* m_worker_thread_ctxs;  // Worker thread contexts, worker_thread_ctx_t[m_max_workers]
            std::atomic<bool>    m_quit;
        };

        // A job will be scheduled as one or many work items depending on how the user wants to schedule it
        static job_handle_t s_schedule_single(worker_thread_ctx_t* ctx, ijob_t* job)
        {
            main_ctx_t* main_ctx = ctx->m_main_ctx;

            u64 job_index = 0;
            queue_dequeue(ctx->m_jobs_free_queue, job_index);
            job_t* job_item          = ctx->m_jobs + job_index;
            job_item->m_job          = job;
            job_item->m_array_length = 1;
            job_item->m_inner_count  = 1;
            job_item->m_dependency   = nullptr;
            job_item->m_done.init(1);

            // Should we enqueue this job on all the worker queues ?
            // The top u32 of the job_index should be the worker thread index that created this job
            job_index = (job_index & 0xFFFFFFFF) | (ctx->m_worker_thread_index << 32);
            queue_enqueue(ctx->m_scheduled_work, job_index);

            // If we have idle worker threads, signal one of them.
            // When there are no idle worker threads, signal all worker threads.

            // Signal the worker thread that there is work to do
            // semaphore_signal(ctx->m_semaphore);

            return (job_handle_t)job_item;
        }

        static job_handle_t s_schedule_for(worker_thread_ctx_t* ctx, ijob_t* job, s32 array_length)
        {
            s32 const thread_index = 0; // Which worker thread should we schedule the job on?

            main_ctx_t* main_ctx = ctx->m_main_ctx;

            u64 job_index = 0;
            queue_dequeue(ctx->m_jobs_free_queue, job_index);
            job_t* job_item          = ctx->m_jobs + job_index;
            job_item->m_job          = job;
            job_item->m_array_length = 1;
            job_item->m_inner_count  = 1;
            job_item->m_dependency   = nullptr;
            job_item->m_done.init(1);

            // Should we enqueue this job on all the worker queues ?
            // The top u32 of the job_index should be the worker thread index that created this job
            job_index = (job_index & 0xFFFFFFFF) | (ctx->m_worker_thread_index << 32);
            queue_enqueue(ctx->m_scheduled_work, job_index);

            // If we have idle worker threads, signal one of them.
            // When there are no idle worker threads, signal all worker threads.

            // Signal the worker thread that there is work to do
            // semaphore_signal(ctx->m_semaphore);

            return (job_handle_t)job_item;
        }

        static job_handle_t s_schedule_parallel(worker_thread_ctx_t* ctx, ijob_t* job, s32 array_length, s32 inner_count)
        {
            main_ctx_t* main_ctx = ctx->m_main_ctx;

            u64 job_index = 0;
            queue_dequeue(ctx->m_jobs_free_queue, job_index);
            job_t* job_item          = ctx->m_jobs + job_index;
            job_item->m_job          = job;
            job_item->m_array_length = 1;
            job_item->m_inner_count  = 1;
            job_item->m_dependency   = nullptr;

            // Schedule job as N work items
            s32 const N = main_ctx->m_max_worker_threads;
            job_item->m_done.init(N);

            // Should we enqueue this job on all the worker queues ?
            // The top u32 of the job_index should be the worker thread index that created this job
            job_index = (job_index & 0xFFFFFFFF) | (ctx->m_worker_thread_index << 32);
            queue_enqueue(ctx->m_scheduled_work, job_index);

            // Signal all the worker threads that there is work to do
            for (s32 i = 0; i < main_ctx->m_max_worker_threads; ++i)
            {
                // semaphore_signal(main_ctx->m_worker_thread_ctxs[i].m_semaphore);
            }

            return (job_handle_t)job_item;
        }

        struct worker_t
        {
            main_ctx_t*          m_main_ctx;
            worker_thread_ctx_t* m_worker_ctx;
            s32                  m_worker_index;
        };

        static void s_worker_thread(worker_t* worker)
        {
            main_ctx_t*          main_ctx     = worker->m_main_ctx;
            worker_thread_ctx_t* this_ctx     = worker->m_worker_ctx;
            s32 const            worker_index = worker->m_worker_index;

            while (!main_ctx->m_quit.load())
            {
                // Wait for work
                u64 work;
                if (!queue_dequeue(this_ctx->m_scheduled_work, work))
                {
                    // No work, add this worker to the idle worker threads queue
                    queue_enqueue(main_ctx->m_idle_worker_threads, worker_index);
                }

                // Get the job object
                // Top part of the u64 is the worker index that created the job
                u32 const job_index = (work >> 32) & 0xFFFFFFFF;
                job_t*    job       = main_ctx->m_worker_thread_ctxs[job_index].m_jobs + job_index;

                // Keep working this job until it is done
                while (true)
                {
                    s32 const work_index = job->m_index.fetch_add(1);

                    // Compute the begin and end of the work item
                    s32 const work_range = job->m_array_length / job->m_inner_count;
                    s32 const work_begin = math::min(work_index * work_range, job->m_array_length);
                    s32 const work_end   = math::min(work_begin + work_range, job->m_array_length);

                    // Make sure that this index is within range
                    if (work_begin < work_end)
                    {
                        job->m_job->execute(work_begin, work_end); // Execute this work range
                    }
                    else
                    {
                        job->m_done.done(); // Mark the job as done
                        break;              // This job is done, move on to the next job
                    }
                }
            }
        }

        // Example of granularity being too fine:
        //    array_length = 10000, inner_count = 10, this means we will schedule 1000 work items.
        //    If we have 4 worker threads, then each worker thread will have 250 work items to process. Also
        //    the amount of stealing will be high which will lead to a lot of contention.
        // Correction to the example above:
        //    array_length = 10000, inner_count = 100, this means we will schedule 100 work items.
        //    If we have 4 worker threads, then each worker thread will have 25 work items to process. Now
        //    work items take longer to process and the amount of stealing will be lower which will lead to
        //    less contention.

        // How to handle dependencies and wait ?

        // What about sync points ?

        // Job handles can point to a struct that contains the job info, 'atomic<bool> finished' and its dependencies
        // WaitForAll(job_handle_t* jobs, int count) means that we look at the job handle and check if the job is done

    } // namespace njob
} // namespace ncore