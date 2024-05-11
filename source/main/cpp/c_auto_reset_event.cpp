#include "ccore/c_target.h"
#include "cbase/c_allocator.h"
#include "cbase/c_integer.h"

#include "cjobs/c_auto_reset_event.h"

#include <atomic>

#ifdef TARGET_PC
#    include <windows.h>
#    undef min
#    undef max
#endif

#ifdef TARGET_MAC
#    include <mach/mach.h>
#    include <mach/task.h>
#    include <mach/semaphore.h>
#endif

#if defined(TARGET_LINUX)
#    include <semaphore.h>
#endif

namespace ncore
{
#if defined(TARGET_PC)
    //---------------------------------------------------------
    // sema_t (Windows)
    //---------------------------------------------------------
    class sema_t
    {
    public:
        HANDLE m_hSema;
        void   init(s32 initialCount = 0)
        {
            ASSERT(initialCount >= 0);
            m_hSema = CreateSemaphore(NULL, initialCount, MAXLONG, NULL);
        }
        void release() { CloseHandle(m_hSema); }

        void wait() { WaitForSingleObject(m_hSema, INFINITE); }
        void signal(s32 count = 1) { ReleaseSemaphore(m_hSema, count, NULL); }
    };

#elif defined(__MACH__)
    //---------------------------------------------------------
    // sema_t (Apple iOS and OSX)
    // Can't use POSIX semaphores due to http://lists.apple.com/archives/darwin-kernel/2009/Apr/msg00010.html
    //---------------------------------------------------------
    class sema_t
    {
    public:
        semaphore_t m_sema;
        void        init(s32 initialCount = 0)
        {
            ASSERT(initialCount >= 0);
            semaphore_create(mach_task_self(), &m_sema, SYNC_POLICY_FIFO, initialCount);
        }

        void release() { semaphore_destroy(mach_task_self(), m_sema); }
        void wait() { semaphore_wait(m_sema); }
        void signal() { semaphore_signal(m_sema); }

        void signal(s32 count)
        {
            while (count-- > 0)
            {
                semaphore_signal(m_sema);
            }
        }
    };

#elif defined(TARGET_LINUX)
    //---------------------------------------------------------
    // sema_t (POSIX, Linux)
    //---------------------------------------------------------

#    include <semaphore.h>

    class sema_t
    {
    public:
        sem_t m_sema;
        void  init(s32 initialCount = 0)
        {
            ASSERT(initialCount >= 0);
            sem_init(&m_sema, 0, initialCount);
        }

        void release() { sem_destroy(&m_sema); }

        void wait()
        {
            // http://stackoverflow.com/questions/2013181/gdb-causes-sem-wait-to-fail-with-eintr-error
            s32 rc;
            do
            {
                rc = sem_wait(&m_sema);
            } while (rc == -1 && errno == EINTR);
        }

        void signal() { sem_post(&m_sema); }
        void signal(s32 count)
        {
            while (count-- > 0)
            {
                sem_post(&m_sema);
            }
        }
    };

#else

#    error Unsupported platform!

#endif
    //---------------------------------------------------------
    // lwmutex_t
    //---------------------------------------------------------
    class lwmutex_t
    {
    public:
        std::atomic<s32> m_contention; // The "box office"
        sema_t           m_sema;       // The "bouncer"

        void init(s32 initialCount = 0)
        {
            m_contention = (initialCount);
            m_sema.init(initialCount);
            ASSERT(initialCount >= 0);
        }

        void release() { m_sema.release(); }

        bool lock()
        {
            if (m_contention.fetch_add(1, std::memory_order_acquire) > 0) // Visit the box office
            {
                m_sema.wait(); // Enter the wait queue
            }
        }

        void unlock()
        {
            if (m_contention.fetch_sub(1, std::memory_order_release) > 1) // Visit the box office
            {
                m_sema.signal(); // Release a waiting thread from the queue
            }
        }
    };

    //---------------------------------------------------------
    // lwsema_t
    //---------------------------------------------------------
    class lwsema_t
    {
    public:
        std::atomic<s32> m_count;
        sema_t           m_sema;

        void init(s32 initialCount = 0)
        {
            m_count = (initialCount);
            m_sema.init(initialCount);
            ASSERT(initialCount >= 0);
        }

        void release() { m_sema.release(); }

        void waitWithPartialSpinning()
        {
            s32 oldCount;
            // Is there a better way to set the initial spin count?
            // If we lower it to 1000, testBenaphore becomes 15x slower on my Core i7-5930K Windows PC,
            // as threads start hitting the kernel semaphore.
            s32 spin = 10000;
            while (spin--)
            {
                oldCount = m_count.load(std::memory_order_relaxed);
                if ((oldCount > 0) && m_count.compare_exchange_strong(oldCount, oldCount - 1, std::memory_order_acquire))
                    return;
                std::atomic_signal_fence(std::memory_order_acquire); // Prevent the compiler from collapsing the loop.
            }
            oldCount = m_count.fetch_sub(1, std::memory_order_acquire);
            if (oldCount <= 0)
            {
                m_sema.wait();
            }
        }

        bool tryWait()
        {
            s32 oldCount = m_count.load(std::memory_order_relaxed);
            return (oldCount > 0 && m_count.compare_exchange_strong(oldCount, oldCount - 1, std::memory_order_acquire));
        }

        void wait()
        {
            if (!tryWait())
                waitWithPartialSpinning();
        }

        void signal(s32 count = 1)
        {
            s32 oldCount  = m_count.fetch_add(count, std::memory_order_release);
            s32 toRelease = -oldCount < count ? -oldCount : count;
            if (toRelease > 0)
            {
                m_sema.signal(toRelease);
            }
        }
    };

    //---------------------------------------------------------
    // autoresetevent_t
    //---------------------------------------------------------
    class autoresetevent_t
    {
    public:
        // m_status == 1: Event object is signaled.
        // m_status == 0: Event object is reset and no threads are waiting.
        // m_status == -N: Event object is reset and N threads are waiting.
        std::atomic<s32> m_status;
        lwsema_t         m_sema;

        void init(s32 initialStatus = 0)
        {
            ASSERT(initialStatus >= 0 && initialStatus <= 1);
            m_status = initialStatus;
            m_sema.init();
        }

        void release() { m_sema.release(); }

        void signal()
        {
            s32 oldStatus = m_status.load(std::memory_order_relaxed);
            for (;;) // Increment m_status atomically via CAS loop.
            {
                ASSERT(oldStatus <= 1);
                s32 const newStatus = oldStatus < 1 ? oldStatus + 1 : 1;
                if (m_status.compare_exchange_weak(oldStatus, newStatus, std::memory_order_release, std::memory_order_relaxed))
                    break;
                // The compare-exchange failed, likely because another thread changed m_status.
                // oldStatus has been updated. Retry the CAS loop.
            }
            if (oldStatus < 0)
                m_sema.signal(); // Release one waiting thread.
        }

        void wait()
        {
            s32 const oldStatus = m_status.fetch_sub(1, std::memory_order_acquire);
            ASSERT(oldStatus <= 1);
            if (oldStatus < 1)
            {
                m_sema.wait();
            }
        }

        DCORE_CLASS_PLACEMENT_NEW_DELETE
    };

    void create(alloc_t* allocator, autoreset_event_t*& event, s32 initialCount)
    {
        autoresetevent_t* e = allocator->construct<autoresetevent_t>();
        e->init(initialCount);
        event = (autoreset_event_t*)e;
    }

    void destroy(alloc_t* allocator, autoreset_event_t* event)
    {
        autoresetevent_t* e = (autoresetevent_t*)event;
        e->release();
        allocator->deallocate(e);
    }

    void wait(autoreset_event_t* event)
    {
        autoresetevent_t* e = (autoresetevent_t*)event;
        e->wait();
    }

    void signal(autoreset_event_t* event, s32 count)
    {
        autoresetevent_t* e = (autoresetevent_t*)event;
        e->signal();
    }

} // namespace ncore