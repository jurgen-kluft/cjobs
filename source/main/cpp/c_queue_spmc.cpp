#include "ccore/c_target.h"
#include "cbase/c_allocator.h"
#include "cbase/c_integer.h"
#include "cjobs/c_queue.h"

#include <atomic>
#include <cassert>
#include <stdexcept>

namespace ncore
{
    namespace spmc
    {
        static constexpr int_t cacheline_size = 64; // std::hardware_destructive_interference_size;

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
                static_assert(sizeof(queue_t) % cacheline_size == 0, "sizeof(queue_t) must be a multiple of cache line size to prevent false sharing between adjacent queues");
                static_assert(offsetof(queue_t, m_tail) - offsetof(queue_t, m_head) == static_cast<std::ptrdiff_t>(cacheline_size), "head and tail must be a cache line apart to prevent false sharing");
            }

            DCORE_CLASS_PLACEMENT_NEW_DELETE

            // non-copyable and non-movable
            queue_t(const queue_t&)            = delete;
            queue_t& operator=(const queue_t&) = delete;

            bool try_push(void* item) { return false; }
            bool try_pop(void* item) { return false; }

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
    } // namespace spmc

    struct spmc_queue_t
    {
    };

    spmc_queue_t* spmc_queue_create(alloc_t* allocator, s32 item_count, s32 item_size)
    {
        s32 const array_size = (item_count + 1) * math::alignUp(item_size, spmc::cacheline_size);
        void*     mem        = allocator->allocate(array_size + sizeof(spmc::queue_t), spmc::cacheline_size);
        if (mem == nullptr)
            return nullptr;
        void*          array_data = ((byte*)mem + sizeof(spmc::queue_t));
        spmc::queue_t* queue      = new (mem) spmc::queue_t(array_data, array_size, item_size);
        return (spmc_queue_t*)queue;
    }

    void queue_destroy(alloc_t* allocator, spmc_queue_t* queue)
    {
        spmc::queue_t* spmc_queue = (spmc::queue_t*)queue;
        allocator->deallocate(spmc_queue);
    }

    bool queue_enqueue(spmc_queue_t* queue, void* item)
    {
        spmc::queue_t* spmc_queue = (spmc::queue_t*)queue;
        return spmc_queue->try_push(item);
    }

    bool queue_dequeue(spmc_queue_t* queue, void* item)
    {
        spmc::queue_t* spmc_queue = (spmc::queue_t*)queue;
        return spmc_queue->try_pop(item);
    }

} // namespace ncore