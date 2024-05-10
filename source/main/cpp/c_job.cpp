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
            ijob_t*       m_job;
            s32           m_array_length;
            s32           m_inner_count;
            job_handle_t* m_dependency;
            job_done_t    m_done;
            s16           m_worker_index; // Worker thread that created this job
        };

        struct work_t
        {
            job_t* m_job;
            s32    m_start;
            s32    m_end;
        };

        // For work items, the thing is they are very temporary, once a job is done all of the work
        // items can be freed. The obvious thing to do is to have a mpmc queue with work items that
        // can be used to alloc and free. However this will cause obvious contention on the queue.

        // Note: There are moments in the execution that can be used to garbage collect, hmmmmm.
        //       Maybe we can have the main thread do the garbage collection? But how do we know
        //       which ranges of work items are done? Should we have a separate queue for jobs that
        //       are done but need to be garbage collected?

        // Note: If we can make more use of SPSC or even MPSC then we can avoid a lot of contention.
        //       For example, one specific worker thread will create a job, preferably with memory that
        //       is local to that worker thread. When any of the workers detect the end of the job it
        //       will push the job on the 'free' queue of the worker thread that created the job. This
        //       way we can avoid contention on the 'free' queue of the main thread.
        //       This does increase the overall memory footprint.

        struct main_context_t;

        struct worker_context_t
        {
            main_context_t* m_ctx;
            s32             m_index;
            // semaphore_t m_semaphore; // Semaphore to signal this worker thread

            u32            m_max_jobs;        // Maximum number of jobs that can be created by this worker
            job_t*         m_jobs;            // Array of jobs, job_t[m_max_jobs]
            local_queue_t* m_jobs_new_queue;  // Queue of jobs that need to be processed
            mpsc_queue_t*  m_jobs_free_queue; // This worker can take a job from this queue and schedule it
            mpsc_queue_t*  m_jobs_done_queue; // These are jobs that are 'done', can be pushed here from any worker thread
            spmc_queue_t* m_scheduled_work;  // Worker thread work queue

            u32   m_max_work_items; // Maximum number of work items that can be active
            byte* m_work_item_mem;  // Large enough memory to hold work items for any created job by this worker
        };

        struct main_context_t
        {
            u32               m_max_workers;      // Number of worker threads
            mpmc_queue_t*     m_inactive_workers; // Worker threads that have no work, queue<s32>
            worker_context_t* m_worker_contexts;  // Worker thread contexts, worker_context_t[m_max_workers]
        };

        // A job will be scheduled as one or many work items depending on how the user wants to schedule it
        static job_handle_t s_schedule_single(main_context_t* ctx, ijob_t* job)
        {
            s32 const thread_index = 0; // Which worker thread should we schedule the job on?

            job_t* job_item = nullptr;
            queue_dequeue(ctx->m_jobs_queue, &job_item);
            job_item->m_job          = job;
            job_item->m_array_length = 1;
            job_item->m_inner_count  = 1;
            job_item->m_dependency   = nullptr;
            job_item->m_done.init(1);

            // Schedule job as one work item
            work_t* work = nullptr;
            queue_dequeue(ctx->m_work_item_queue, &work);
            work->m_job   = job_item;
            work->m_start = 0;
            work->m_end   = 1;
            queue_enqueue(ctx->m_scheduled_work[thread_index], &work);

            return (job_handle_t)job_item;
        }

        static job_handle_t s_schedule_for(main_context_t* ctx, ijob_t* job, s32 array_length)
        {
            s32 const thread_index = 0; // Which worker thread should we schedule the job on?

            job_t* job_item = nullptr;
            queue_dequeue(ctx->m_jobs_queue, &job_item);
            job_item->m_job          = job;
            job_item->m_array_length = array_length;
            job_item->m_inner_count  = array_length;
            job_item->m_dependency   = nullptr;
            job_item->m_done.init(1);

            // Schedule job as one work item
            work_t* work = nullptr;
            queue_dequeue(ctx->m_work_item_queue, &work);
            work->m_job   = job_item;
            work->m_start = 0;
            work->m_end   = array_length;
            queue_enqueue(ctx->m_scheduled_work[thread_index], &work);

            return (job_handle_t)job_item;
        }

        static job_handle_t s_schedule_parallel(main_context_t* ctx, ijob_t* job, s32 array_length, s32 innerloop_batch_count)
        {
            s32 const thread_index = 0; // Which worker thread should we schedule the job on?

            job_t* job_item = nullptr;
            queue_dequeue(ctx->m_jobs_queue, &job_item);
            job_item->m_job          = job;
            job_item->m_array_length = array_length;
            job_item->m_inner_count  = innerloop_batch_count;
            job_item->m_dependency   = nullptr;

            // Schedule job as N work items
            s32 const N = (array_length + (innerloop_batch_count - 1)) / innerloop_batch_count;
            job_item->m_done.init(N);

            s32 start = 0;
            while (start < array_length)
            {
                s32 const end = math::min(start + innerloop_batch_count, array_length);

                work_t* work = nullptr;
                queue_dequeue(ctx->m_work_item_queue, &work);
                work->m_job   = job_item;
                work->m_start = start;
                work->m_end   = end;
                queue_enqueue(ctx->m_scheduled_work[thread_index], &work);

                start = end;
            }

            return (job_handle_t)job_item;
        }

        struct worker_t
        {
            main_context_t*   m_main_ctx;
            worker_context_t* m_worker_ctx;
            s32               m_index;
        };

        static void s_worker_thread(worker_t* worker)
        {
            main_context_t* ctx   = worker->m_main_ctx;
            s32 const       index = worker->m_index;

            while (true)
            {
                // Wait for work
                work_t* work = nullptr;
                queue_dequeue(worker->m_worker_ctx->m_scheduled_work, &work);

                if (work == nullptr)
                {
                    // No work, steal work from other worker queues
                    for (s32 i = 0; i < ctx->m_max_workers; ++i)
                    {
                        if (i == index)
                            continue;
                        worker_context_t* worker_ctx = &ctx->m_worker_contexts[i];
                        if (queue_dequeue(worker_ctx->m_scheduled_work, &work))
                            break;
                    }
                }

                if (work == nullptr)
                {
                    // No work, find a job
                    continue;
                }

                // The job that work is part of
                job_t* job = work->m_job;

                // Execute work
                job->m_job->execute(work->m_start, work->m_end);

                if (job->m_done.done())
                {
                    // Job is done, return it to the worker that created it
                    s32 const worker_index = job->m_worker_index;
                    queue_enqueue(ctx->m_worker_contexts[worker_index].m_jobs_done_queue, &job);
                }

                // Return work item ?
                // The whole range of work items can be release once this job itself is done
                // So no need to actually free work items like this
                // queue_enqueue(ctx->m_work_item_queue, &work);
            }
        }

        // Example of granularity being too fine:
        //    array_length = 10000, innerloop_batch_count = 10, this means we will schedule 1000 work items.
        //    If we have 4 worker threads, then each worker thread will have 250 work items to process. Also
        //    the amount of stealing will be high which will lead to a lot of contention.
        // Correction to the example above:
        //    array_length = 10000, innerloop_batch_count = 100, this means we will schedule 100 work items.
        //    If we have 4 worker threads, then each worker thread will have 25 work items to process. Now
        //    work items take longer to process and the amount of stealing will be lower which will lead to
        //    less contention.

        // How to handle dependencies and wait ?

        // What about sync points ?

        // Job handles can point to a struct that contains the job info, 'atomic<bool> finished' and its dependencies
        // WaitForAll(job_handle_t* jobs, int count) means that we look at the job handle and check if the job is done

    } // namespace njob
} // namespace ncore