#include "ccore/c_target.h"
#include "cbase/c_allocator.h"
#include "cbase/c_integer.h"

#include "cjobs/private/c_signal.h"

#include <atomic>
#include <mutex>
#include <condition_variable>

namespace ncore
{
    struct signal_t
    {
        std::atomic<bool>           m_is_set;
        std::mutex                  m_mutex;
        std::condition_variable     m_condition;

        DCORE_CLASS_PLACEMENT_NEW_DELETE
    };

    void signal_create(alloc_t* allocator, signal_t*& event)
    {
        event = g_construct<signal_t>(allocator);
        event->m_is_set.store(false, std::memory_order_relaxed);
    }

    void signal_destroy(alloc_t* allocator, signal_t* event)
    {
        if (event != nullptr)
        {
            g_destruct<signal_t>(allocator, event);
        }
    }

    bool signal_set(signal_t* event)
    {
        bool was_set = event->m_is_set.exchange(true, std::memory_order_acq_rel);
        if (!was_set)
        {
            event->m_condition.notify_one();
        }
        return !was_set;
    }

    void signal_reset(signal_t* event)
    {
        event->m_is_set.store(false, std::memory_order_release);
    }

    void signal_wait(signal_t* event, bool autoReset)
    {
        std::unique_lock<std::mutex> lock(event->m_mutex);
        event->m_condition.wait(lock, [event] { return event->m_is_set.load(std::memory_order_acquire); });
        
        if (autoReset)
        {
            event->m_is_set.store(false, std::memory_order_release);
        }
    }

}