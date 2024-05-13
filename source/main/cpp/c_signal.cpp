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
    class __signal_t
    {
    public:
        // m_status == 1: Event object is signaled.
        // m_status == 0: Event object is reset and no threads are waiting.
        // m_status == -N: Event object is reset and N threads are waiting.
        std::atomic<s32> m_status;
#ifdef TARGET_PC
        HANDLE m_event;
#endif
#ifdef TARGET_MAC
        semaphore_t m_sema;
#endif

        void init(s32 initialStatus = 0)
        {
            ASSERT(initialStatus >= 0 && initialStatus <= 1);
            m_status = initialStatus;
#ifdef TARGET_PC
            m_event = CreateEvent(nullptr, FALSE, initialStatus == 1, nullptr);
#endif
#ifdef TARGET_MAC
            semaphore_create(mach_task_self(), &m_sema, SYNC_POLICY_FIFO, initialStatus);
#endif
        }

        void release()
        {
#ifdef TARGET_PC
            CloseHandle(m_event);
#endif
#ifdef TARGET_MAC
            semaphore_destroy(mach_task_self(), m_sema);
#endif
        }

        void reset() { m_status.store(0, std::memory_order_relaxed); }

        bool signal()
        {
            s32 const previousStatus = m_status.fetch_or(1, std::memory_order_relaxed);
            if ((previousStatus & 2) == 2)
            {
#ifdef TARGET_PC
                SetEvent(m_event);
#endif
#ifdef TARGET_MAC
                semaphore_signal_all(m_sema);
#endif
                return true;
            }
            return false;
        }

        bool is_signaled() const { return (m_status.load(std::memory_order_relaxed) & 1) == 1; }

        void wait()
        {
            s32 const previousStatus = m_status.fetch_or(2, std::memory_order_acquire);
            if (previousStatus == 0)
            {
#ifdef TARGET_PC
                WaitForSingleObject(m_event, INFINITE);
#endif
#ifdef TARGET_MAC
                semaphore_wait(m_sema);
#endif
            }
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

    bool signal_isset(signal_t* event)
    {
        __signal_t* e = (__signal_t*)event;
        return e->is_signaled();
    }

} // namespace ncore