#include "ccore/c_target.h"
#include "cbase/c_allocator.h"
#include "cbase/c_integer.h"

#include "cjobs/private/c_signal.h"

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
    // __sema_t (Windows)
    //---------------------------------------------------------

#    include <windows.h>
#    undef min
#    undef max

    class __sema_t
    {
    private:
        HANDLE m_hSema;

        __sema_t(const __sema_t& other)            = delete;
        __sema_t& operator=(const __sema_t& other) = delete;

    public:
        void create(s32 initialCount = 0)
        {
            ASSERT(initialCount >= 0);
            m_hSema = CreateSemaphore(NULL, initialCount, MAXLONG, NULL);
        }

        void destroy() { CloseHandle(m_hSema); }
        void wait() { WaitForSingleObject(m_hSema, INFINITE); }
        void signal(s32 count = 1) { ReleaseSemaphore(m_hSema, count, NULL); }
    };

#elif defined(TARGET_MAC)
    //---------------------------------------------------------
    // __sema_t (Apple iOS and OSX)
    // Can't use POSIX semaphores due to http://lists.apple.com/archives/darwin-kernel/2009/Apr/msg00010.html
    //---------------------------------------------------------

#    include <mach/mach.h>

    class __sema_t
    {
    private:
        semaphore_t m_sema;

        __sema_t(const __sema_t& other)            = delete;
        __sema_t& operator=(const __sema_t& other) = delete;

    public:
        void create(s32 initialCount = 0)
        {
            ASSERT(initialCount >= 0);
            semaphore_create(mach_task_self(), &m_sema, SYNC_POLICY_FIFO, initialCount);
        }

        void destroy() { semaphore_destroy(mach_task_self(), m_sema); }
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
    // __sema_t (POSIX, Linux)
    //---------------------------------------------------------

#    include <semaphore.h>

    class __sema_t
    {
    private:
        sem_t m_sema;

        __sema_t(const __sema_t& other)            = delete;
        __sema_t& operator=(const __sema_t& other) = delete;

    public:
        void create(s32 initialCount = 0)
        {
            ASSERT(initialCount >= 0);
            sem_init(&m_sema, 0, initialCount);
        }

        void destroy() { sem_destroy(&m_sema); }

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

    class __signal_t
    {
    public:
        // m_status >= 1: Signal is set.
        // m_status == 0: Signal is idle and no threads are waiting.
        // m_status == -N: Signal has N threads waiting and is not set yet.
        std::atomic<s32> m_status;
        __sema_t         m_sema;

        void init()
        {
            m_status = 0;
            m_sema.create(0);
        }

        void release() { m_sema.destroy(); }

        bool signal_new()
        {
            // NOTE can we not just do a large add ?
            s32 oldStatus = m_status.fetch_add(1 << 20, std::memory_order_relaxed);
            if (oldStatus < 0)
                m_sema.signal(-oldStatus); // Release all waiting threads.
            // Return true if we were the first to signal.
            return oldStatus <= 0;
        }

        bool signal()
        {
            s32 oldStatus = m_status.load(std::memory_order_relaxed);
            for (;;) // Increment atomically via CAS loop.
            {
                s32 const newStatus = 1 << 30; // Set the signal so high that calls to wait will not block anymore
                if (m_status.compare_exchange_weak(oldStatus, newStatus, std::memory_order_release, std::memory_order_relaxed))
                    break;
                // The compare-exchange failed, likely because another thread changed m_status.
                // oldStatus has been updated. Retry the CAS loop.
            }
            if (oldStatus < 0)
                m_sema.signal(-oldStatus); // Release all waiting threads.

            // Return true if we were the first to signal.
            return oldStatus <= 0;
        }

        void reset()
        {
            signal();
            m_status.store(0, std::memory_order_relaxed);
        }

        void wait()
        {
            s32 const previousStatus = m_status.fetch_sub(1, std::memory_order_acquire);
            if (previousStatus < 1)
            {
                m_sema.wait();
            }
        }

        DCORE_CLASS_PLACEMENT_NEW_DELETE
    };

    // This is a counter that can be incremented by multiple threads and decremented by one thread.
    // A use-case is a queue where multiple threads can add items and one thread can remove items.
    // The counter indicates how many items are in the queue and the thread removing items can wait
    // for items to be added if the queue is empty.
    class __counter_t
    {
    public:
        // m_counter == +N: This indicates that there are N items in the queue.
        // m_counter ==  0: This indicates that the queue is empty.
        // m_counter == -1: This indicates that the queue was empty and a thread is now waiting.
        std::atomic<s32> m_counter;
        __sema_t         m_sema;

        void init()
        {
            m_counter = 0;
            m_sema.create(0);
        }

        void release() { m_sema.destroy(); }

        void increment()
        {
            s32 oldCounter = m_counter.load(std::memory_order_relaxed);
            for (;;) // Increment atomically via CAS loop.
            {
                s32 const newCounter = oldCounter <= 0 ? 0 : oldCounter + 1;
                if (m_counter.compare_exchange_weak(oldCounter, newCounter, std::memory_order_release, std::memory_order_relaxed))
                    break;
                // The compare-exchange failed, likely because another thread changed m_counter.
                // oldCounter has been updated. Retry the CAS loop.
            }
            if (oldCounter < 0)
                m_sema.signal(); // Release one waiting thread.
        }

        // Can only be called by one thread, returns the items available so
        // that the caller knows it can decrement the counter more and doing
        // so can empty the queue without triggering the 'wait'.
        s32 decrement()
        {
            s32 const previousCounter = m_counter.fetch_sub(1, std::memory_order_acquire);
            if (previousCounter < 1)
            {
                m_sema.wait();
                return 1; // One item is available again
            }
            return previousCounter;
        }

        DCORE_CLASS_PLACEMENT_NEW_DELETE
    };

    void signal_create(alloc_t* allocator, signal_t*& event)
    {
        __signal_t* e = allocator->construct<__signal_t>();
        e->init();
        event = (signal_t*)e;
    }

    void signal_destroy(alloc_t* allocator, signal_t* event)
    {
        __signal_t* e = (__signal_t*)event;
        e->release();
        allocator->deallocate(e);
    }

    void signal_wait(signal_t* event)
    {
        __signal_t* e = (__signal_t*)event;
        e->wait();
    }

    bool signal_set(signal_t* event)
    {
        __signal_t* e = (__signal_t*)event;
        return e->signal();
    }

    void signal_reset(signal_t* event)
    {
        __signal_t* e = (__signal_t*)event;
        e->reset();
    }

} // namespace ncore
