#include "ccore/c_target.h"
#include "cbase/c_allocator.h"
#include "cbase/c_integer.h"
#include "cjobs/c_queue.h"

#include <atomic>
#include <cassert>
#include <stdexcept>

namespace ncore
{
    namespace mpsc
    {
 static constexpr int_t c_cacheline_size = 64;
        static constexpr u32   c_item_size      = 8;

        struct local_ring_t
        {
            u64* m_slots;
            u64  m_capacity;
            u32  m_writer;
            u32  m_reader;

            local_ring_t(u64* slots, u32 capacity)
                : m_slots(slots)
                , m_capacity(capacity)
                , m_writer(0)
                , m_reader(0)
            {
            }

            bool is_empty() const { return m_reader == m_writer; }
            u32  cursor() const { return m_reader; }

            void add(u64 item)
            {
                m_slots[m_writer++] = item;
                if (m_writer == m_capacity)
                    m_writer = 0;
            }

            void add_multiple(const u64* items, u32 count)
            {
                for (u32 i = 0; i < count; ++i)
                {
                    m_slots[m_writer++] = items[i];
                    if (m_writer == m_capacity)
                        m_writer = 0;
                }
            }

            bool inspect(u32& begin, u32& end) const
            {
                if (is_empty())
                    return false;
                begin = m_reader;
                end   = m_writer;
                return true;
            }

            u64 read(u32& idx) const
            {
                u32 const i = idx;
                idx++;
                if (idx == m_capacity)
                    idx = 0;
                return m_slots[i];
            }
        };

        local_ring_t* create_ring_buffer(alloc_t* allocator, u32 capacity)
        {
            u32 const size   = sizeof(local_ring_t) + sizeof(u64) * capacity;
            u8*       buffer = (u8*)allocator->allocate(size);
            if (buffer == nullptr)
                return nullptr;
            local_ring_t* ring = new (new_signature(), buffer) local_ring_t((u64*)(buffer + sizeof(local_ring_t)), capacity);
            return ring;
        }

        struct queue_t
        {
            alignas(c_cacheline_size) std::atomic<s32> m_queues_state;
            u32            m_num_queues;
            spsc_queue_t** m_producer_queues;
            local_ring_t*  m_consumer_ring;

            void push(u32 producerIdx, u64 item)
            {
                spsc_queue_t* queue = m_producer_queues[producerIdx];
                queue_enqueue(queue, item);
                m_queues_state.fetch_or(1 << producerIdx, std::memory_order_release);
            }

            // Return true when we have a range of elements available, return false when none
            // of the queues have pending elements.
            bool inspect(u32& begin, u32& end)
            {
                // Fetch the state of the queues and reset to 0
                s32 old_state = m_queues_state.fetch_and(0, std::memory_order_acquire);
                if (old_state == 0)
                    return false;

                // Drain every producer queue, using batch dequeue
                u64       items[8];
                s32 const max_items = 8;
                for (s32 i = 0; i < m_num_queues; ++i)
                {
                    spsc_queue_t* queue = m_producer_queues[i];
                    while (true)
                    {
                        s32 const num_dequeued = queue_dequeue_multiple(queue, items, max_items);
                        if (num_dequeued == 0)
                            break;
                        m_consumer_ring->add_multiple(items, num_dequeued);
                    }
                }
                return m_consumer_ring->inspect(begin, end);
            }

            u64 read(u32& idx) const { return m_consumer_ring->read(idx); }

            s8 release(u32 idx, u32 end)
            {
                ASSERT(m_consumer_ring->cursor() == end);
                s8 result = m_consumer_ring->is_empty() ? 0 : 1;
                if (result == 0)
                {
                    s32 const state = m_queues_state.load(std::memory_order_acquire);
                    result          = state == 0 ? 0 : 1;
                }
                return result;
            }

            DCORE_CLASS_PLACEMENT_NEW_DELETE
        };

        queue_t* create_queue(alloc_t* allocator, s32 producer_count, s32 item_count)
        {
            queue_t* queue           = allocator->construct<queue_t>();
            queue->m_num_queues      = producer_count;
            queue->m_producer_queues = (spsc_queue_t**)allocator->allocate(producer_count * sizeof(spsc_queue_t*));

            // We are going to drain the producer queues into the consumer ring buffer so we need to have
            // enough capacity to hold all the elements of every producer queue.
            queue->m_consumer_ring = create_ring_buffer(allocator, producer_count * item_count);
            queue->m_queues_state.store(0, std::memory_order_relaxed);
            for (s32 i = 0; i < producer_count; ++i)
            {
                queue->m_producer_queues[i] = spsc_queue_create(allocator, item_count);
            }
            return queue;
        }
    } // namespace mpsc

    struct mpsc_queue_t
    {
    };

    mpsc_queue_t* mpsc_queue_create(alloc_t* allocator, s32 producer_count, s32 item_count)
    {
        mpsc::queue_t* queue = mpsc::create_queue(allocator, producer_count, item_count);
        return (mpsc_queue_t*)queue;
    }

    void queue_destroy(alloc_t* allocator, mpsc_queue_t* _queue)
    {
        mpsc::queue_t* spmc_queue = (mpsc::queue_t*)_queue;
        allocator->deallocate(spmc_queue);
    }

    bool queue_enqueue(mpsc_queue_t* _queue, u32 producerIdx, u64 item)
    {
        mpsc::queue_t* queue = (mpsc::queue_t*)_queue;
        queue->push(producerIdx, item);
        return true;
    }

    bool queue_inspect(mpsc_queue_t* _queue, u32& begin, u32& end)
    {
        mpsc::queue_t* queue = (mpsc::queue_t*)_queue;
        return queue->inspect(begin, end);
    }

    bool queue_dequeue(mpsc_queue_t* _queue, u32& idx, u32 end, u64& item)
    {
        mpsc::queue_t* queue = (mpsc::queue_t*)_queue;
        item                 = queue->read(idx);
        return true;
    }

    s8 queue_release(mpsc_queue_t* _queue, u32 idx, u32 end)
    {
        mpsc::queue_t* queue = (mpsc::queue_t*)_queue;
        return queue->release(idx, end);
    }
} // namespace ncore
