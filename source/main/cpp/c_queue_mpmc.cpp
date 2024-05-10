#include "ccore/c_target.h"
#include "cbase/c_allocator.h"
#include "cbase/c_integer.h"
#include "cjobs/c_queue.h"

#include <atomic>
#include <cassert>
#include <stdexcept>

namespace ncore
{
    namespace mpmc
    {
        static constexpr int_t cacheline_size = 64; // std::hardware_destructive_interference_size;

        struct slot_t
        {
            std::atomic<s32> turn;
            // storage ....

            inline void store(void* item, u32 item_size) noexcept
            {
                byte*             dst = (byte*)((byte*)this + sizeof(slot_t));
                byte const*       src = (byte const*)item;
                byte const* const end = src + item_size;
                while (src < end)
                    *dst++ = *src++;
            }

            inline void retrieve(void* item, u32 item_size) noexcept
            {
                byte const* src = (byte*)((byte*)this + sizeof(slot_t));
                byte*       dst = (byte*)item;
                byte* const end = dst + item_size;
                while (dst < end)
                    *dst++ = *src++;
            }
        };

        class queue_t
        {
        public:
            explicit queue_t(void* array, u32 array_size, u32 item_size)
                : m_head(0)
                , m_producer((byte*)array, item_size, ((item_size + cacheline_size - 1) / cacheline_size) * cacheline_size, array_size)
                , m_tail(0)
                , m_consumer((byte*)array, item_size, ((item_size + cacheline_size - 1) / cacheline_size) * cacheline_size, array_size)
            {
                ASSERTS((int_t)array % cacheline_size == 0, "array must be aligned to cache line boundary to prevent false sharing");
                static_assert(sizeof(queue_t) == 2 * cacheline_size, "sizeof(queue_t) must be a multiple of cache line size to prevent false sharing between adjacent queues");
                static_assert(offsetof(queue_t, m_tail) - offsetof(queue_t, m_head) == static_cast<std::ptrdiff_t>(cacheline_size), "head and tail must be a cache line apart to prevent false sharing");
            }

            DCORE_CLASS_PLACEMENT_NEW_DELETE

            // non-copyable and non-movable
            queue_t(const queue_t&)            = delete;
            queue_t& operator=(const queue_t&) = delete;

            void push(void* item) noexcept
            {
                auto const head = m_head.fetch_add(1);
                auto&      slot = *((slot_t*)(m_producer.m_slots + (m_producer.idx(head) * m_producer.m_slot_size)));
                while (m_producer.turn(head) * 2 != slot.turn.load(std::memory_order_acquire)) {}
                slot.store(item, m_producer.m_item_size);
                slot.turn.store(m_producer.turn(head) * 2 + 1, std::memory_order_release);
            }

            bool try_push(void* item) noexcept
            {
                auto head = m_head.load(std::memory_order_acquire);
                for (;;)
                {
                    auto& slot = *((slot_t*)(m_producer.m_slots + (m_producer.idx(head) * m_producer.m_slot_size)));

                    if (m_producer.turn(head) * 2 == slot.turn.load(std::memory_order_acquire))
                    {
                        if (m_head.compare_exchange_strong(head, head + 1))
                        {
                            slot.store(item, m_producer.m_item_size);
                            slot.turn.store(m_producer.turn(head) * 2 + 1, std::memory_order_release);
                            return true;
                        }
                    }
                    else
                    {
                        auto const prevHead = head;
                        head                = m_head.load(std::memory_order_acquire);
                        if (head == prevHead)
                        {
                            return false;
                        }
                    }
                }
            }

            void pop(void* item) noexcept
            {
                auto const tail = m_tail.fetch_add(1);
                auto&      slot = *((slot_t*)(m_consumer.m_slots + (m_consumer.idx(tail) * m_consumer.m_slot_size)));
                while (m_consumer.turn(tail) * 2 + 1 != slot.turn.load(std::memory_order_acquire)) {}
                slot.retrieve(item, m_consumer.m_item_size);
                slot.turn.store(m_consumer.turn(tail) * 2 + 2, std::memory_order_release);
            }

            bool try_pop(void* item) noexcept
            {
                auto tail = m_tail.load(std::memory_order_acquire);
                for (;;)
                {
                    auto& slot = *((slot_t*)(m_consumer.m_slots + (m_consumer.idx(tail) * m_consumer.m_slot_size)));
                    if (m_consumer.turn(tail) * 2 + 1 == slot.turn.load(std::memory_order_acquire))
                    {
                        if (m_tail.compare_exchange_strong(tail, tail + 1))
                        {
                            slot.retrieve(item, m_consumer.m_item_size);
                            slot.turn.store(m_consumer.turn(tail) * 2 + 2, std::memory_order_release);
                            return true;
                        }
                    }
                    else
                    {
                        auto const prevTail = tail;
                        tail                = m_tail.load(std::memory_order_acquire);
                        if (tail == prevTail)
                        {
                            return false;
                        }
                    }
                }
            }

            struct header_t
            {
                header_t(byte* slots, s32 item_size, s32 slot_size, s32 capacity)
                    : m_slots(slots)
                    , m_item_size(item_size)
                    , m_slot_size(slot_size)
                    , m_capacity((capacity / slot_size) - 1)
                    , m_dummy(0)
                {
                }

                constexpr s32 idx(s32 i) const noexcept { return i % m_capacity; }
                constexpr s32 turn(s32 i) const noexcept { return i / m_capacity; }

                byte* const m_slots;
                s32 const   m_item_size;
                s32 const   m_slot_size;
                s32 const   m_capacity;
                s32 const   m_dummy;
            };

            // Align to avoid false sharing between head and tail
            alignas(cacheline_size) std::atomic<s32> m_head;
            header_t m_producer;
            alignas(cacheline_size) std::atomic<s32> m_tail;
            header_t m_consumer;
        };
    } // namespace mpmc

    struct mpmc_queue_t
    {
    };

    mpmc_queue_t* mpmc_queue_create(alloc_t* allocator, s32 item_count, s32 item_size)
    {
        s32 const array_size = (item_count + 1) * math::alignUp(item_size + sizeof(mpmc::slot_t), mpmc::cacheline_size);
        void*     mem        = allocator->allocate(array_size + sizeof(mpmc::queue_t), mpmc::cacheline_size);
        if (mem == nullptr)
            return nullptr;
        void*          array_data = ((byte*)mem + sizeof(mpmc::queue_t));
        mpmc::queue_t* queue      = new (mem) mpmc::queue_t(array_data, array_size, item_size);
        return (mpmc_queue_t*)queue;
    }

    void queue_destroy(alloc_t* allocator, mpmc_queue_t* queue)
    {
        mpmc::queue_t* mpmc_queue = (mpmc::queue_t*)queue;
        allocator->deallocate(mpmc_queue);
    }

    bool queue_enqueue(mpmc_queue_t* queue, void* item)
    {
        mpmc::queue_t* mpmc_queue = (mpmc::queue_t*)queue;
        return mpmc_queue->try_push(item);
    }

    bool queue_dequeue(mpmc_queue_t* queue, void* item)
    {
        mpmc::queue_t* mpmc_queue = (mpmc::queue_t*)queue;
        return mpmc_queue->try_pop(item);
    }

} // namespace ncore