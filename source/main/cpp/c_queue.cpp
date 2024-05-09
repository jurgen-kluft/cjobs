#include "ccore/c_target.h"
#include "cbase/c_allocator.h"
#include "cbase/c_integer.h"
#include "cjobs/c_queue.h"

#include <atomic>
#include <cassert>
#include <cstddef> // offsetof
#include <limits>
#include <memory>
#include <new> // std::hardware_destructive_interference_size
#include <stdexcept>

namespace ncore
{
    namespace mpmc
    {
        static constexpr int_t cacheline_size = 64; // std::hardware_destructive_interference_size;

        struct slot_t
        {
            std::atomic<int_t> turn;
            // storage ....

            void store(void* item, u32 item_size) noexcept
            {
                byte* storage = (byte*)((byte*)this + sizeof(std::atomic<int_t>));
                std::memcpy(storage, item, item_size);
            }

            void retrieve(void* item, u32 item_size) noexcept
            {
                byte const* storage = (byte*)((byte*)this + sizeof(std::atomic<int_t>));
                std::memcpy(item, storage, item_size);
            }
        };

        class queue_t
        {
        public:
            explicit queue_t(void* array, u32 array_size, u32 item_size)
                : m_item_size(item_size)
                , m_slot_size(((item_size + sizeof(slot_t) + cacheline_size - 1) / cacheline_size) * cacheline_size)
                , m_slots((byte*)array)
                , m_capacity((array_size / m_slot_size) - 1) // Note: We need one additional (dummy) slot to prevent false sharing on the last slot
                , m_head(0)
                , m_tail(0)
            {
                ASSERTS((int_t)m_slots % cacheline_size == 0, "array must be aligned to cache line boundary to prevent false sharing");
                static_assert(sizeof(queue_t) % cacheline_size == 0, "sizeof(queue_t) must be a multiple of cache line size to prevent false sharing between adjacent queues");
                static_assert(offsetof(queue_t, m_tail) - offsetof(queue_t, m_head) == static_cast<std::ptrdiff_t>(cacheline_size), "head and tail must be a cache line apart to prevent false sharing");
            }

            DCORE_CLASS_PLACEMENT_NEW_DELETE

            // non-copyable and non-movable
            queue_t(const queue_t&)            = delete;
            queue_t& operator=(const queue_t&) = delete;

            void push(void* item) noexcept
            {
                auto const head = m_head.fetch_add(1);
                auto&      slot = *((slot_t*)(m_slots + (idx(head) * m_slot_size)));
                while (turn(head) * 2 != slot.turn.load(std::memory_order_acquire)) {}
                slot.store(item, m_item_size);
                slot.turn.store(turn(head) * 2 + 1, std::memory_order_release);
            }

            bool try_push(void* item) noexcept
            {
                auto head = m_head.load(std::memory_order_acquire);
                for (;;)
                {
                    auto& slot = *((slot_t*)(m_slots + (idx(head) * m_slot_size)));

                    if (turn(head) * 2 == slot.turn.load(std::memory_order_acquire))
                    {
                        if (m_head.compare_exchange_strong(head, head + 1))
                        {
                            slot.store(item, m_item_size);
                            slot.turn.store(turn(head) * 2 + 1, std::memory_order_release);
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
                auto&      slot = *((slot_t*)(m_slots + (idx(tail) * m_slot_size)));
                while (turn(tail) * 2 + 1 != slot.turn.load(std::memory_order_acquire)) {}
                slot.retrieve(item, m_item_size);
                slot.turn.store(turn(tail) * 2 + 2, std::memory_order_release);
            }

            bool try_pop(void* item) noexcept
            {
                auto tail = m_tail.load(std::memory_order_acquire);
                for (;;)
                {
                    auto& slot = *((slot_t*)(m_slots + (idx(tail) * m_slot_size)));
                    if (turn(tail) * 2 + 1 == slot.turn.load(std::memory_order_acquire))
                    {
                        if (m_tail.compare_exchange_strong(tail, tail + 1))
                        {
                            slot.retrieve(item, m_item_size);
                            slot.turn.store(turn(tail) * 2 + 2, std::memory_order_release);
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

            /// Returns the number of elements in the queue.
            /// The size can be negative when the queue is empty and there is at least one
            /// reader waiting. Since this is a concurrent queue the size is only a best
            /// effort guess until all reader and writer threads have been joined.
            int_t size() const noexcept
            {
                // TODO: How can we deal with wrapped queue on 32bit?
                return static_cast<int_t>(m_head.load(std::memory_order_relaxed) - m_tail.load(std::memory_order_relaxed));
            }

            /// Returns true if the queue is empty.
            /// Since this is a concurrent queue this is only a best effort guess
            /// until all reader and writer threads have been joined.
            bool empty() const noexcept { return size() <= 0; }

            constexpr int_t idx(int_t i) const noexcept { return i % m_capacity; }
            constexpr int_t turn(int_t i) const noexcept { return i / m_capacity; }

            const int_t m_item_size;
            const int_t m_slot_size;
            byte* const m_slots;
            const int_t m_capacity;

            // Align to avoid false sharing between head and tail
            alignas(cacheline_size) std::atomic<int_t> m_head;
            alignas(cacheline_size) std::atomic<int_t> m_tail;
        };
    } // namespace mpmc

    namespace njob
    {
        struct queue_t
        {
        };

        queue_t* create_queue(alloc_t* allocator, s32 item_count, s32 item_size)
        {
            s32 const array_size = (item_count + 1) * math::alignUp(item_size + sizeof(mpmc::slot_t), mpmc::cacheline_size);
            void*     mem        = allocator->allocate(array_size + sizeof(mpmc::queue_t), mpmc::cacheline_size);
            if (mem == nullptr)
                return nullptr;
            void*          array_data = ((byte*)mem + sizeof(mpmc::queue_t));
            mpmc::queue_t* queue      = new (mem) mpmc::queue_t(array_data, array_size, item_size);
            return (queue_t*)queue;
        }

        void destroy_queue(alloc_t* allocator, queue_t* queue)
        {
            mpmc::queue_t* mpmc_queue = (mpmc::queue_t*)queue;
            allocator->deallocate(mpmc_queue);
        }

        void queue_enqueue(queue_t* queue, void* item)
        {
            mpmc::queue_t* mpmc_queue = (mpmc::queue_t*)queue;
            mpmc_queue->push(item);
        }

        bool queue_dequeue(queue_t* queue, void* item)
        {
            mpmc::queue_t* mpmc_queue = (mpmc::queue_t*)queue;
            return mpmc_queue->try_pop(item);
        }

    } // namespace njob

} // namespace ncore