#include "ccore/c_target.h"
#include "cbase/c_allocator.h"
#include "cjobs/c_queue.h"
#include "cjobs/c_job.h"

namespace ncore
{
    namespace njob
    {
        // Use std::thread
        // https://en.cppreference.com/w/cpp/thread

        struct job_ranges_t
        {
            s32  BatchSize;
            s32  NumJobs;
            s32  TotalIterationCount;
            s32* StartEndIndex;
        };

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

        struct job_schedule_parameters_t
        {
            job_handle_t m_dependency;
            s32          m_schedule_mode;
            void*        m_job_data;

            job_schedule_parameters_t(void* job_data, job_handle_t dependency, EScheduleMode scheduleMode)
            {
                m_dependency    = dependency;
                m_job_data      = job_data;
                m_schedule_mode = (s32)scheduleMode;
            }
        };

        struct queue_t
        {
            ijob* dequeue() { return nullptr; }
        };

        struct semaphore_t
        {
            void wait(u32 ms);
        };

        struct worker_t
        {
            bool          isQuitting;
            s32           m_workerId;
            queue_t*      m_worker_queue;
            semaphore_t** m_semaphores;

            void  execute();
            ijob* steal_from_other_queues();
            void  wake_workers();
            void  execute_job(ijob* job);
            bool  should_sleep();
        };

        void worker_t::execute()
        {
            while (!isQuitting)
            {
                // Take a job from our worker thread’s local queue
                ijob* pJob = m_worker_queue[m_workerId].dequeue();

                // If our queue is empty try to steal work from someone
                // else's queue to help them out.
                if (pJob == nullptr)
                {
                    pJob = steal_from_other_queues();
                }

                if (pJob)
                {
                    // If we found work, there may be more conditionally
                    // wake up other workers as necessary
                    wake_workers();
                    execute_job(pJob);
                }
                // Conditionally go to sleep (perhaps we were told there is a
                // parallel job we can help with)
                else if (should_sleep())
                {
                    // Put the thread to sleep until more jobs are scheduled
                    m_semaphores[m_workerId]->wait(1);
                }
            }
        }

        /// <summary>
        /// The maximum number of job threads that can ever be created by the job system.
        /// </summary>
        /// <remarks>This maximum is the theoretical max the job system supports. In practice, the maximum number of job worker threads
        /// created by the job system will be lower as the job system will prevent creating more job worker threads than logical
        /// CPU cores on the target hardware. This value is useful for compile time constants, however when used for creating buffers
        /// it may be larger than required. For allocating a buffer that can be subdivided evenly between job worker threads, prefer
        /// the runtime constant returned by <seealso cref="JobsUtility.ThreadIndexCount"/>.
        /// </remarks>
        const int  CacheLineSize         = 64;
        const int  MaxJobThreadCount     = 128;
        static int JobWorkerMaximumCount = 128;

        class jobs_utility_t
        {
            static void GetJobRange(job_ranges_t& ranges, s32 jobIndex, s32& outBeginIndex, s32& outEndIndex)
            {
                s32 const* startEndIndices = (s32 const*)ranges.StartEndIndex;
                outBeginIndex              = startEndIndices[jobIndex * 2];
                outEndIndex                = startEndIndices[jobIndex * 2 + 1];
            }

            static int  GetJobQueueWorkerThreadCount();
            static void SetJobQueueMaximumActiveThreadCount(int count);
            static void ResetJobWorkerCount();
            static int  GetJobWorkerCount() { return GetJobQueueWorkerThreadCount(); }
            static void SetJobWorkerCount(int value)
            {
                if ((value < 0) || (value > JobWorkerMaximumCount))
                {
                    ASSERT(false);
                    return;
                }
                SetJobQueueMaximumActiveThreadCount(value);
            }

            // <summary>
            // Returns the index for the current thread when executing a job, otherwise 0.
            // When multiple threads are executing jobs, no two threads will have the same index.
            // Range is [0, ThreadIndexCount].
            // </summary>
            static int ThreadIndex() { return 0; }

            /// <summary>
            /// Returns the maximum number of job workers that can work on a job at the same time.
            /// </summary>
            /// <remarks>
            /// The job system will create a number of job worker threads that will be no greater than the number of logical CPU cores for the platform.
            /// However, since arbitrary threads can execute jobs via work stealing we allocate extra workers which act as temporary job worker threads.
            // JobsUtility.ThreadIndexCount reflects the maximum number of job worker threads plus temporary workers the job system will ever use.
            // As such, this value is useful for allocating buffers which should be subdivided evenly between job workers since ThreadIndex will never
            // return a value greater than ThreadIndexCount.
            /// </remarks>
            static int ThreadIndexCount() { return GetJobWorkerCount(); }
        };

        // -----------------------------------------------------------------------------------------------------------------------
        // Job System

        class system_t
        {
        public:
            void setup(s32 num_workers = 1);
            void teardown();

            u32        m_num_workers;
            worker_t** m_workers;
            queue_t**  m_queues;
        };

    } // namespace njob
} // namespace ncore