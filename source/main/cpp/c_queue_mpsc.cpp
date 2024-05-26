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

        struct slot_t
        {
            std::atomic<s32> turn;
            s32              dummy;
            u64              item;
            // storage ....
            s64 padding[6];
        };

        class queue_t
        {
        public:
            explicit queue_t(slot_t* array, u32 array_size)
                : m_producer(array, array_size)
                , m_consumer(array, array_size)
            {
                static_assert(sizeof(slot_t) == c_cacheline_size, "sizeof(slot_t) must be cache line size");
                static_assert(sizeof(header_t) == c_cacheline_size, "sizeof(header_t) must be cache line size");
                static_assert(sizeof(queue_t) == 2 * c_cacheline_size, "sizeof(queue_t) must be a multiple of cache line size to prevent false sharing between adjacent queues");
                static_assert(offsetof(queue_t, m_consumer.m_index) - offsetof(queue_t, m_producer.m_index) == static_cast<std::ptrdiff_t>(c_cacheline_size), "head and tail must be a cache line apart to prevent false sharing");
            }

            DCORE_CLASS_PLACEMENT_NEW_DELETE

            // non-copyable and non-movable
            queue_t(const queue_t&)            = delete;
            queue_t& operator=(const queue_t&) = delete;

            // Unguarded, this will not check if the queue is full
            void push(u64 item) noexcept
            {
                auto const head = m_producer.m_index.fetch_add(1);
                auto&      slot = *(m_producer.m_slots + m_producer.idx(head));
                while (m_producer.turn(head) * 2 != slot.turn.load(std::memory_order_acquire)) {}
                slot.item = item;
                slot.turn.store(m_producer.turn(head) * 2 + 1, std::memory_order_release);
            }

            bool try_push(u64 item) noexcept
            {
                auto head = m_producer.m_index.load(std::memory_order_acquire);
                for (;;)
                {
                    auto& slot = *((slot_t*)(m_producer.m_slots + (m_producer.idx(head) * c_item_size)));

                    if (m_producer.turn(head) * 2 == slot.turn.load(std::memory_order_acquire))
                    {
                        if (m_producer.m_index.compare_exchange_strong(head, head + 1))
                        {
                            slot.item = item;
                            slot.turn.store(m_producer.turn(head) * 2 + 1, std::memory_order_release);

                            // NOTE we signal here a semaphore
                            //   m_sema.signal()

                            return true;
                        }
                    }
                    else
                    {
                        auto const prevHead = head;
                        head                = m_producer.m_index.load(std::memory_order_acquire);
                        if (head == prevHead)
                        {
                            return false;
                        }
                    }
                }
            }

            // Unguarded, this will not check if the queue is empty
            void pop(u64& item) noexcept
            {
                auto const tail = m_consumer.m_index.fetch_add(1);
                auto&      slot = *(m_consumer.m_slots + m_consumer.idx(tail));
                while (m_consumer.turn(tail) * 2 + 1 != slot.turn.load(std::memory_order_acquire)) {}
                item = slot.item;
                slot.turn.store(m_consumer.turn(tail) * 2 + 2, std::memory_order_release);
            }

            bool try_pop(u64& item) noexcept
            {
                auto tail = m_consumer.m_index.load(std::memory_order_acquire);
                for (;;)
                {
                    auto& slot = *(m_consumer.m_slots + m_consumer.idx(tail));
                    if (m_consumer.turn(tail) * 2 + 1 == slot.turn.load(std::memory_order_acquire))
                    {
                        if (m_consumer.m_index.compare_exchange_strong(tail, tail + 1))
                        {
                            item = slot.item;
                            slot.turn.store(m_consumer.turn(tail) * 2 + 2, std::memory_order_release);
                            return true;
                        }
                    }
                    else
                    {
                        auto const prevTail = tail;
                        tail                = m_consumer.m_index.load(std::memory_order_acquire);
                        if (tail == prevTail)
                        {
                            // NOTE we wait here on a semaphore to be signalled
                            //   m_sema.wait()
                            //   continue;

                            return false;
                        }
                    }
                }
            }

            struct header_t
            {
                header_t(slot_t* slots, s32 capacity)
                    : m_index(0)
                    , m_pad0(0)
                    , m_slots(slots)
                    , m_capacity((capacity / c_item_size) - 1)
                {
                }

                constexpr s32 idx(s32 i) const noexcept { return i % m_capacity; }
                constexpr s32 turn(s32 i) const noexcept { return i / m_capacity; }

                std::atomic<s32> m_index;
                s32 const        m_pad0;
                slot_t* const    m_slots;
                s32 const        m_capacity;
                s32              m_padding[16 - 5];
            };

            // Align to avoid false sharing between head and tail
            header_t m_producer; // head
            header_t m_consumer; // tail
        };
    } // namespace mpsc

    struct mpsc_queue_t
    {
    };

    mpsc_queue_t* mpsc_queue_create(alloc_t* allocator, s32 item_count)
    {
        s32 const array_size = (item_count + 1) * sizeof(mpsc::slot_t);
        void*     mem        = allocator->allocate(sizeof(mpsc::queue_t) + array_size, mpsc::c_cacheline_size);
        if (mem == nullptr)
            return nullptr;
        mpsc::slot_t*  array_data = (mpsc::slot_t*)((byte*)mem + sizeof(mpsc::queue_t));
        ASSERTS(math::isAligned((int_t)array_data, mpsc::c_cacheline_size), "array must be aligned to cache line boundary to prevent false sharing");
        mpsc::queue_t* queue      = new (mem) mpsc::queue_t(array_data, array_size);
        return (mpsc_queue_t*)queue;
    }

    void queue_destroy(alloc_t* allocator, mpsc_queue_t* queue)
    {
        mpsc::queue_t* spmc_queue = (mpsc::queue_t*)queue;
        allocator->deallocate(spmc_queue);
    }

    bool queue_enqueue(mpsc_queue_t* queue, u64 item)
    {
        mpsc::queue_t* spmc_queue = (mpsc::queue_t*)queue;
        return spmc_queue->try_push(item);
    }

    bool queue_dequeue(mpsc_queue_t* queue, u64& item)
    {
        mpsc::queue_t* spmc_queue = (mpsc::queue_t*)queue;
        return spmc_queue->try_pop(item);
    }

} // namespace ncore
