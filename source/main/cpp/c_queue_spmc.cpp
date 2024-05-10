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
                : m_item_size(item_size)
                , m_slot_size(((item_size + cacheline_size - 1) / cacheline_size) * cacheline_size)
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

            bool try_push(void* item) { return false; }
            bool try_pop(void* item) { return false; }

            /// Returns true if the queue is empty.
            /// Since this is a concurrent queue this is only a best effort guess
            /// until all reader and writer threads have been joined.
            s32        size() const noexcept { return m_head.load(std::memory_order_relaxed) - m_tail.load(std::memory_order_relaxed); }
            bool       empty() const noexcept { return size() <= 0; }
            inline s32 capacity() const noexcept { return m_capacity; }

            constexpr s32 idx(s32 i) const noexcept { return i % m_capacity; }
            constexpr s32 turn(s32 i) const noexcept { return i / m_capacity; }

            const s32   m_item_size;
            const s32   m_slot_size;
            byte* const m_slots;
            const s32   m_capacity; // Note: If capacity is a power of 2, we can use a mask instead of modulo and a shift operation instead of a division

            // Align to avoid false sharing between head and tail
            alignas(cacheline_size) std::atomic<s32> m_head;
            alignas(cacheline_size) std::atomic<s32> m_tail;
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