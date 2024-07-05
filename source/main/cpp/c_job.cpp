#include "ccore/c_target.h"
#include "cbase/c_allocator.h"
#include "cbase/c_integer.h"

#include "cjobs/c_queue.h"
#include "cjobs/c_job.h"
#include "cjobs/private/c_signal.h"

#include <atomic>
#include <thread>

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
            work_t*          m_dependency;       // Job that needs to be done before this job can be executed
            signal_t*        m_signal;           // Signal to wait on when this job is not done
            std::atomic<s32> m_work_indexx;      // Index, begin and end can be calculated from job->m_total_iter_count and job->m_inner_iter_count
            std::atomic<s32> m_done_index;       // Index, when this is equal to (total_iter_count / inner_iter_count) then the this job is done
            std::atomic<s32> m_ref_count;        // Reference count, when this is zero then the job can be returned to the free queue of the context where it was created
            s32              m_total_iter_count; // Total loop iteration count (e.g. array length)
            s32              m_inner_iter_count; // Inner loop iteration count
            s32              m_worker_index;     // Worker thread that owns this job
            s32              m_dummy;            // Padding to make sure that this struct is 64 bytes
            void*            m_dummy3[3];        // Padding to make sure that this struct is 64 bytes
        };

        struct context_t
        {
            s16            m_worker_index;    //
            s16            m_max_jobs;        // Maximum number of jobs that can be created by this worker
            work_t*        m_jobs;            // Array of jobs, work_t[m_max_jobs]
            local_queue_t* m_jobs_free_queue; // This worker can take a new job from this queue and initialize it
            mpsc_queue_t*  m_jobs_done_queue; // These are jobs that are 'done', can be pushed here from any worker thread
            mpsc_queue_t*  m_scheduled_work;  // Worker thread work queue
            s16            m_max_scheduling;  //
            job_t**        m_scheduling;      //
            signal_t*      m_signal;          // Worker will wait on this signal when there is no work to do
        };

        struct system_t;

        struct worker_t
        {
            system_t const* m_system;
            context_t*      m_worker_ctx;
            std::thread     m_thread;
        };

        struct system_t
        {
            context_t* m_contexts;             // Thread contexts, context_t[m_contexts_num]
            worker_t*  m_workers;              // The worker instances
            u16        m_contexts_num;         // Number of contexts
            u16        m_workers_num;          // Number of workers, also means number of worker threads
            u16        m_max_threads;          // Maximum number of threads that can be recognized, this also means threads that interact with the job system
            u16        m_num_threads;          // The number of recognized threads
            u64*       m_thread_id_to_context; // Every thread has a context
        };

        // This is the thread function that each worker thread runs to execute jobs
        static void s_worker_thread(worker_t* worker);

        void g_create(alloc_t* allocator, system_t*& system, s16 num_workers, s32 max_running_threads)
        {
            s16 const max_jobs    = 1024;
            s16 const max_threads = num_workers;

            // Now we also need to know the maximum number of producers and the user might also want to
            // have some producer indices since he also wants to push

            system                 = (system_t*)allocator->allocate(sizeof(system_t));
            system->m_num_threads  = num_workers;
            system->m_workers_num  = num_workers;
            system->m_contexts_num = max_running_threads;
            system->m_contexts     = (context_t*)allocator->allocate(max_running_threads * sizeof(context_t));

            // Create worker threads
            for (s16 i = 0; i < num_workers; ++i)
            {
                context_t* ctx         = system->m_contexts + i;
                ctx->m_worker_index    = i;
                ctx->m_max_jobs        = max_jobs;
                ctx->m_jobs            = (work_t*)allocator->allocate(ctx->m_max_jobs * sizeof(work_t));
                ctx->m_jobs_done_queue = mpsc_queue_create(allocator, num_workers, ctx->m_max_jobs);
                ctx->m_scheduled_work  = mpsc_queue_create(allocator, num_workers, ctx->m_max_jobs);
                ctx->m_max_scheduling  = 64;
                ctx->m_scheduling      = (job_t**)allocator->allocate(ctx->m_max_scheduling * sizeof(job_t*));
            }

            // Prepare the 'done' queue with all the available jobs

            // Create worker objects
            system->m_workers = (worker_t*)allocator->allocate(num_workers * sizeof(worker_t));
            for (s16 i = 0; i < num_workers; ++i)
            {
                worker_t* worker     = &system->m_workers[i];
                worker->m_system     = system;
                worker->m_worker_ctx = &system->m_contexts[i];
            }

            // Spin up the worker threads, using std::thread
            for (s16 i = 0; i < num_workers; ++i)
            {
                worker_t* worker = &system->m_workers[i];
                worker->m_thread = std::thread(s_worker_thread, worker);
            }
        }

        static const u64 s_quit_work = 0xdeadbeefcafebabe;

        void g_destroy(alloc_t* allocator, system_t*& system)
        {
            // All of the worker threads could be 'sleeping' due to no work, push a
            // 'quit' job onto the queue to wake them up and have them exit their loop.

            s32 this_producer = 0;
            for (s32 i = 0; i < system->m_workers_num; ++i)
            {
                context_t* ctx = &system->m_contexts[i];
                queue_enqueue(ctx->m_scheduled_work, this_producer, s_quit_work);
                signal_set(ctx->m_signal);
            }

            for (s32 i = 0; i < system->m_workers_num; ++i)
            {
                worker_t* worker = &system->m_workers[i];
                worker->m_thread.join();
            }

            for (s32 i = 0; i < system->m_workers_num; ++i)
            {
                context_t* ctx = &system->m_contexts[i];
                queue_destroy(allocator, ctx->m_jobs_done_queue);
                queue_destroy(allocator, ctx->m_scheduled_work);
                allocator->deallocate(ctx->m_jobs);
            }
            allocator->deallocate(system->m_contexts);
            allocator->deallocate(system->m_workers);
            allocator->deallocate(system);
            system = nullptr;
        }

        inline static void s_job_create(work_t* job, alloc_t* allocator) { signal_create(allocator, job->m_signal); }
        inline static void s_job_destroy(work_t* job, alloc_t* allocator) { signal_destroy(allocator, job->m_signal); }
        inline static bool s_job_set_done(work_t* job) { return signal_set(job->m_signal); }
        inline static void s_job_wait_until_done(work_t* job) { signal_wait(job->m_signal, false); }

        inline static u64  s_encode_work_ticket(u32 worker_index, u32 job_index) { return ((u64)worker_index << 32) | (u64)job_index; }
        inline static void s_decode_work_ticket(u64 ticket, u32& out_worker_index, u32& out_job_index)
        {
            out_worker_index = (u32)(ticket >> 32);
            out_job_index    = (u32)ticket;
        }

        inline static s32 s_find_or_register_slot_for_thread_id(system_t* system, u64 thread_id)
        {
            // skip the worker contexts
            for (s32 i = system->m_workers_num; i < system->m_num_threads; ++i)
            {
                if (system->m_thread_id_to_context[i] == thread_id)
                {
                    return i;
                }
            }

            // Register this thread
            s32 const slot                       = system->m_num_threads++;
            system->m_thread_id_to_context[slot] = thread_id;
            return slot;
        }

        // A job will be scheduled as one or many work items depending on how the user wants to schedule it
        // Main context is the context that is associated with the thread that is initiating the request for scheduling
        // this job.
        static void s_schedule_single(system_t* system, context_t* main_ctx, job_t* job)
        {
            context_t* this_ctx = &system->m_contexts[0];

            u64 job_index = 0;
            queue_dequeue(this_ctx->m_jobs_free_queue, job_index);
            work_t* work             = this_ctx->m_jobs + job_index;
            work->m_job              = job;
            work->m_work_indexx      = 0;
            work->m_done_index       = 1;
            work->m_ref_count        = system->m_workers_num;
            work->m_total_iter_count = 1;
            work->m_inner_iter_count = 1;
            work->m_dependency       = nullptr;

            u64 const work_ticket = s_encode_work_ticket(this_ctx->m_worker_index, job_index);

            for (s32 j = 0; j < system->m_workers_num; ++j)
            {
                context_t* worker_ctx = &system->m_contexts[j];
                queue_enqueue(worker_ctx->m_scheduled_work, worker_ctx->m_worker_index, work_ticket);
            }

            for (s32 j = 0; j < system->m_workers_num; ++j)
            {
                context_t* worker_ctx = &system->m_contexts[j];
                signal_set(worker_ctx->m_signal);
            }
        }

        static void s_schedule_for(system_t* system, context_t* main_ctx, job_t* job, s32 array_length)
        {
            u64 job_index = 0;
            queue_dequeue(main_ctx->m_jobs_free_queue, job_index);
            work_t* work             = main_ctx->m_jobs + job_index;
            work->m_job              = job;
            work->m_work_indexx      = 0;
            work->m_done_index       = 1;
            work->m_ref_count        = system->m_workers_num;
            work->m_total_iter_count = array_length;
            work->m_inner_iter_count = array_length;
            work->m_dependency       = nullptr;

            u64 const work_ticket = s_encode_work_ticket(main_ctx->m_worker_index, job_index);

            for (s32 j = 0; j < system->m_workers_num; ++j)
            {
                context_t* worker_ctx = &system->m_contexts[j];
                queue_enqueue(worker_ctx->m_scheduled_work, worker_ctx->m_worker_index, work_ticket);
            }

            for (s32 j = 0; j < system->m_workers_num; ++j)
            {
                context_t* worker_ctx = &system->m_contexts[j];
                signal_set(worker_ctx->m_signal);
            }
        }

        static void s_schedule_parallel(system_t* system, context_t* main_ctx, job_t* job, s32 array_length, s32 inner_count)
        {
            u64 job_index = 0;
            queue_dequeue(main_ctx->m_jobs_free_queue, job_index);
            work_t* work             = main_ctx->m_jobs + job_index;
            work->m_job              = job;
            work->m_work_indexx      = 0;
            work->m_done_index       = (array_length + (inner_count - 1)) / inner_count;
            work->m_ref_count        = system->m_workers_num;
            work->m_total_iter_count = array_length;
            work->m_inner_iter_count = inner_count;
            work->m_dependency       = nullptr;

            u64 const work_ticket = s_encode_work_ticket(main_ctx->m_worker_index, job_index);
            for (s32 j = 0; j < system->m_workers_num; ++j)
            {
                context_t* worker_ctx = &system->m_contexts[j];
                queue_enqueue(worker_ctx->m_scheduled_work, worker_ctx->m_worker_index, work_ticket);
            }

            for (s32 j = 0; j < system->m_workers_num; ++j)
            {
                context_t* worker_ctx = &system->m_contexts[j];
                signal_set(worker_ctx->m_signal);
            }
        }

        static void s_worker_thread(worker_t* worker)
        {
            system_t const* main_ctx     = worker->m_system;
            context_t*      this_ctx     = worker->m_worker_ctx;
            s32 const       worker_index = worker->m_worker_ctx->m_worker_index;

            bool quit = false;
            while (!quit)
            {
                u32 i, e;
                if (!queue_inspect(this_ctx->m_scheduled_work, i, e))
                {
                    signal_wait(this_ctx->m_signal, true);
                    continue;
                }

                // Ok, we have N = 'e - i' jobs to process.
                u32 const begin = i;
                u32 const end   = e;

                while (i < end)
                {
                    // Process the local job queue
                    u64 work_ticket;
                    queue_dequeue(this_ctx->m_scheduled_work, i, end, work_ticket);

                    quit = work_ticket == s_quit_work;
                    if (quit)
                        break;

                    // Top part of the u64 is the worker index that created the job
                    u32 job_index, owner_index;
                    s_decode_work_ticket(work_ticket, owner_index, job_index);

                    // Get the job object
                    work_t* job = &main_ctx->m_contexts[owner_index].m_jobs[job_index];

                    // Keep working this job until it is done
                    while (true)
                    {
                        // Increment the work index and compute the begin and end of our work item
                        s32 const work_index = job->m_work_indexx.fetch_add(1, std::memory_order_acquire);

                        // Compute the begin and end index of the iteration range
                        s32 const work_begin = math::min(work_index * job->m_inner_iter_count, job->m_total_iter_count);
                        s32 const work_end   = math::min(work_begin + job->m_inner_iter_count, job->m_total_iter_count);

                        // Make sure that this work range is valid
                        if (work_begin < work_end)
                        {
                            job->m_job->job_execute(work_begin, work_end); // Execute this work range

                            // Are we the worker thread that finished this job?
                            s32 const done_index = job->m_done_index.fetch_sub(1, std::memory_order_acquire);
                            if (done_index == 0)
                            {
                                s_job_set_done(job);

                                // Schedule new jobs
                                s32 const num_jobs = job->m_job->job_finished(this_ctx->m_scheduling, this_ctx->m_max_scheduling); // Notify the user that this job is done

                                // TODO Schedule these new jobs for all worker threads
                            }
                        }
                        else
                        {
                            s32 const ref_count = job->m_ref_count.fetch_sub(1, std::memory_order_acquire);
                            if (ref_count == 0)
                            {
                                // All workers that had this job have processed it, so we can safely return the job to the free queue
                                // of the context that owns it.
                                queue_enqueue(main_ctx->m_contexts[owner_index].m_jobs_done_queue, this_ctx->m_worker_index, job_index);
                            }
                            break;
                        }
                    }
                }

                // Ok, we have processed all the jobs in the range that was obtained, now release this range
                queue_release(this_ctx->m_scheduled_work, begin, end);
            }
        }

        // Example of granularity being too fine:
        //    array_length = 10000, inner_count = 10, this means we will schedule 1000 work items.
        //    If we have 4 worker threads, then each worker thread will have 250 work items to process.
        //    Furthermore the chance of contention will be high which will lead to reduced performance.
        // Simple change to the example above:
        //    array_length = 10000, inner_count = 100, this means we will schedule 100 work items.
        //    If we have 4 worker threads, then each worker thread will have 25 work items to process. Now
        //    work items take longer to process and the chance of contention will be lower which will lead to
        //    better performance.

        void g_schedule(system_t* system, u64 current_thread_id, job_t* job)
        {
            s32 const ctx_slot = s_find_or_register_slot_for_thread_id(system, current_thread_id);
            s_schedule_single(system, &system->m_contexts[ctx_slot], job);
        }

        void g_schedule_single(system_t* system, u64 current_thread_id, job_t* job, s32 totalIterCount)
        {
            s32 const ctx_slot = s_find_or_register_slot_for_thread_id(system, current_thread_id);
            s_schedule_for(system, &system->m_contexts[ctx_slot], job, totalIterCount);
        }

        void g_schedule_parallel(system_t* system, u64 current_thread_id, job_t* job, s32 totalIterCount, s32 innerIterCount)
        {
            s32 const ctx_slot = s_find_or_register_slot_for_thread_id(system, current_thread_id);
            s_schedule_parallel(system, &system->m_contexts[ctx_slot], job, totalIterCount, innerIterCount);
        }
    } // namespace njob

    // -----------------------------------------------------------------------------------------------------------------------
    // -----------------------------------------------------------------------------------------------------------------------
    // -----------------------------------------------------------------------------------------------------------------------
    // -----------------------------------------------------------------------------------------------------------------------

    namespace njob
    {
        // -----------------------------------------------------------------------------------------------------------------------
        // Node is used to integrate a job into the graph
        // -----------------------------------------------------------------------------------------------------------------------
        struct node_t
        {
            job_t*  m_job;              // The job that this node represents
            s32     m_total_iter_count; //
            s32     m_inner_iter_count; //
            group_t m_group;            // The group this job belongs to
            node_t* m_next;
            node_t* m_prev;

            void setup(job_t* job, s32 totalIterCount = 1, s32 innerIterCount = 1)
            {
                m_job              = job;
                m_total_iter_count = totalIterCount;
                m_inner_iter_count = innerIterCount;
                m_next             = this;
                m_prev             = this;
            }

            void push_back(node_t* node)
            {
                node->m_next   = this;
                node->m_prev   = m_prev;
                m_prev->m_next = node;
                m_prev         = node;
            }
        };

        // -----------------------------------------------------------------------------------------------------------------------
        // A group is the main object of the graph and holds a list of jobs
        // -----------------------------------------------------------------------------------------------------------------------
        struct group_t
        {
            const char* m_name;   // Name of the group (AI, Physics, Animation, etc.)
            group_t*    m_parent; // The parent of this group
            group_t*    m_child;  // Children of this group
            node_t*     m_jobs;   // Doubly Linked List of jobs that are part of this group
            group_t*    m_next;   //
            group_t*    m_prev;   //

            void setup()
            {
                m_name   = nullptr;
                m_parent = nullptr;
                m_child  = nullptr;
                m_next   = this;
                m_prev   = this;
            }

            void add_sibling(group_t* group)
            {
                group->m_next  = this;
                group->m_prev  = m_prev;
                m_prev->m_next = group;
                m_prev         = group;
            }

            void add_child(group_t* group)
            {
                group->m_parent = this;
                if (m_child == nullptr)
                {
                    m_child = group;
                }
                else
                {
                    m_child->add_sibling(group);
                }
            }

            void add_job(node_t* node)
            {
                if (m_jobs == nullptr)
                {
                    m_jobs = node;
                }
                else
                {
                    m_jobs->push_back(node);
                }
            }
        };

        // -----------------------------------------------------------------------------------------------------------------------
        // The graph is the main data structure used to hold a tree of groups
        // -----------------------------------------------------------------------------------------------------------------------
        struct graph_t
        {
            s32             m_max_jobs;
            s32             m_alloc_mem_size;
            alloc_buffer_t* m_allocator;
            u16*            m_sorted_jobs; // Sorted, sort key is the 'job' in 'job_array'
            job_t**         m_jobs;
            group_t*        m_current;
            void*           m_alloc_mem;
        };

        // -----------------------------------------------------------------------------------------------------------------------
        // Static utility functions
        // -----------------------------------------------------------------------------------------------------------------------

        static group_t* new_group(graph_t* graph, const char* name)
        {
            group_t* group = graph->m_allocator->construct<group_t>();
            if (group == nullptr)
                return nullptr;
            group->setup();
            group->m_name = name;
            return group;
        }

        // -----------------------------------------------------------------------------------------------------------------------
        // -----------------------------------------------------------------------------------------------------------------------

        graph_t* g_createGraph(alloc_t* allocator, system_t* system, s32 maxJobs, s32 maxGroups)
        {
            graph_t* graph = (graph_t*)allocator->allocate(sizeof(graph_t));
            if (graph == nullptr)
                return nullptr;

            graph->m_alloc_mem_size = 1 * 1024 * 1024;
            graph->m_alloc_mem      = allocator->allocate(graph->m_alloc_mem_size);

            alloc_buffer_t* alloc = allocator->construct<alloc_buffer_t>();
            alloc->init((byte*)graph->m_alloc_mem, graph->m_alloc_mem_size);
            graph->m_allocator   = alloc;
            graph->m_max_jobs    = maxJobs;
            graph->m_sorted_jobs = (u16*)allocator->allocate(sizeof(u16) * maxJobs);
            graph->m_jobs        = (job_t**)allocator->allocate(sizeof(job_t*) * maxJobs);

            return graph;
        }

        void g_destroyGraph(alloc_t* allocator, graph_t*& graph)
        {
            if (graph == nullptr)
                return;

            allocator->deallocate(graph->m_jobs);
            allocator->deallocate(graph);
            graph = nullptr;
        }

        void graph_reset(graph_t* graph)
        {
            graph->m_allocator->reset();
            graph->m_current = graph->m_allocator->construct<group_t>();
            graph->m_current->setup();
        }

        void graph_push_group(graph_t* graph, const char* name)
        {
            // the new group becomes a child of the current group, if this group already has a child
            // then the new group should be added to the list of siblings of that child.
            group_t* group = new_group(graph, name);
            if (group == nullptr)
                return;
            graph->m_current->add_child(group);
        }

        void graph_pop_group(graph_t* graph)
        {
            ASSERT(graph->m_current->m_parent != nullptr);
            graph->m_current = graph->m_current->m_parent; // the current group becomes the parent of the current group
        }

        void graph_add_job(graph_t* graph, job_t* job)
        {
            node_t* node = (node_t*)graph->m_allocator->allocate(sizeof(node_t));
            node->setup(job);
            graph->m_current->add_job(node);
        }

        void graph_execute(graph_t* graph) {}

    } // namespace njob
} // namespace ncore
