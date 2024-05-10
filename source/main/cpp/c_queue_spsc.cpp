#include "ccore/c_target.h"
#include "cbase/c_allocator.h"
#include "cbase/c_integer.h"
#include "cjobs/c_queue.h"

#include <atomic>
#include <cassert>
#include <stdexcept>

namespace ncore
{
    namespace spsc
    {
        static constexpr int_t cacheline_size = 64; // std::hardware_destructive_interference_size;

        class queue_t
        {
        public:
            // Note: We need one additional (dummy) slot to prevent false sharing on the last slot
            explicit queue_t(void* array, u32 array_size, u32 item_size)
                : m_writeIdx(0)
                , m_producer((byte*)array, item_size, ((item_size + cacheline_size - 1) / cacheline_size) * cacheline_size, array_size)
                , m_readIdxCache(0)
                , m_readIdx(0)
                , m_consumer((byte*)array, item_size, ((item_size + cacheline_size - 1) / cacheline_size) * cacheline_size, array_size)
                , m_writeIdxCache(0)
            {
                static_assert(alignof(queue_t) == cacheline_size, "");
                static_assert(sizeof(queue_t) == 2 * cacheline_size, "");
                assert(reinterpret_cast<char*>(&m_readIdx) - reinterpret_cast<char*>(&m_writeIdx) >= static_cast<std::ptrdiff_t>(cacheline_size));
            }

            // non-copyable and non-movable
            queue_t(const queue_t&)            = delete;
            queue_t& operator=(const queue_t&) = delete;

            void push(void* item)
            {
                auto const writeIdx     = m_writeIdx.load(std::memory_order_relaxed);
                auto       nextWriteIdx = writeIdx + 1;
                if (nextWriteIdx == m_producer.m_capacity)
                {
                    nextWriteIdx = 0;
                }
                while (nextWriteIdx == m_readIdxCache)
                {
                    m_readIdxCache = m_readIdx.load(std::memory_order_acquire);
                }

                // store item
                byte*             dst = m_producer.m_slots + (writeIdx * m_producer.m_slot_size);
                byte const* const end = dst + m_producer.m_item_size;
                byte const*       src = (byte const*)item;
                while (dst < end)
                    *dst++ = *src++;

                m_writeIdx.store(nextWriteIdx, std::memory_order_release);
            }

            inline bool try_push(void* item)
            {
                auto const writeIdx     = m_writeIdx.load(std::memory_order_relaxed);
                auto       nextWriteIdx = writeIdx + 1;
                if (nextWriteIdx == m_producer.m_capacity)
                {
                    nextWriteIdx = 0;
                }
                if (nextWriteIdx == m_readIdxCache)
                {
                    m_readIdxCache = m_readIdx.load(std::memory_order_acquire);
                    if (nextWriteIdx == m_readIdxCache)
                    {
                        return false;
                    }
                }

                // store item
                byte*             dst = m_producer.m_slots + (writeIdx * m_producer.m_slot_size);
                byte const* const end = dst + m_producer.m_item_size;
                byte const*       src = (byte const*)item;
                while (dst < end)
                    *dst++ = *src++;

                m_writeIdx.store(nextWriteIdx, std::memory_order_release);
                return true;
            }

            bool try_pop(void* item) noexcept
            {
                auto const readIdx = m_readIdx.load(std::memory_order_relaxed);
                if (readIdx == m_writeIdxCache)
                {
                    m_writeIdxCache = m_writeIdx.load(std::memory_order_acquire);
                    if (m_writeIdxCache == readIdx)
                    {
                        return false;
                    }
                }

                // retrieve item
                byte const*       src = m_consumer.m_slots + (readIdx * m_consumer.m_slot_size);
                byte const* const end = src + m_consumer.m_item_size;
                byte*             dst = (byte*)item;
                while (src < end)
                    *dst++ = *src++;

                auto nextReadIdx = readIdx + 1;
                if (nextReadIdx == m_consumer.m_capacity)
                {
                    nextReadIdx = 0;
                }
                m_readIdx.store(nextReadIdx, std::memory_order_release);

                return true;
            }

            inline s32 size() const noexcept
            {
                std::ptrdiff_t diff = m_writeIdx.load(std::memory_order_acquire) - m_readIdx.load(std::memory_order_acquire);
                if (diff < 0)
                {
                    diff += m_consumer.m_capacity;
                }
                return static_cast<s32>(diff);
            }

            inline bool empty() const noexcept { return m_writeIdx.load(std::memory_order_acquire) == m_readIdx.load(std::memory_order_acquire); }
            inline s32  capacity() const noexcept { return m_consumer.m_capacity; }

            DCORE_CLASS_PLACEMENT_NEW_DELETE

            struct header_t
            {
                header_t(byte* slots, s32 item_size, s32 slot_size, s32 capacity)
                    : m_slot_size(slot_size)
                    , m_slots(slots)
                    , m_item_size(item_size)
                    , m_capacity((capacity / slot_size) - 1)
                {
                }
                byte* const m_slots;
                s32 const   m_slot_size;
                s32 const   m_item_size;
                s32 const   m_capacity;
            };

            // Align to cache line size in order to avoid false sharing
            // m_readIdxCache and m_writeIdxCache is used to reduce the amount of cache coherency traffic

            // Producer
            alignas(cacheline_size) std::atomic<s32> m_writeIdx;
            header_t m_producer;
            s32      m_readIdxCache;
            // Is this necessary ?, this is already the cacheline of the producer.
            // alignas(cacheline_size) s32 m_readIdxCache;

            // Consumer
            alignas(cacheline_size) std::atomic<s32> m_readIdx;
            header_t m_consumer;
            s32      m_writeIdxCache;
            // Is this necessary ?, this is already the cacheline of the consumer.
            // alignas(cacheline_size) s32 m_writeIdxCache;
        };
    } // namespace spsc

    struct spsc_queue_t
    {
    };

    spsc_queue_t* spsc_queue_create(alloc_t* allocator, s32 item_count, s32 item_size)
    {
        s32 const array_size = (item_count + 1) * math::alignUp(item_size, spsc::cacheline_size);
        void*     mem        = allocator->allocate(array_size + sizeof(spsc::queue_t), spsc::cacheline_size);
        if (mem == nullptr)
            return nullptr;
        void*          array_data = ((byte*)mem + sizeof(spsc::queue_t));
        spsc::queue_t* queue      = new (mem) spsc::queue_t(array_data, array_size, item_size);
        return (spsc_queue_t*)queue;
    }

    void queue_destroy(alloc_t* allocator, spsc_queue_t* queue)
    {
        spsc::queue_t* spsc_queue = (spsc::queue_t*)queue;
        allocator->deallocate(spsc_queue);
    }

    bool queue_enqueue(spsc_queue_t* queue, void* item)
    {
        spsc::queue_t* spsc_queue = (spsc::queue_t*)queue;
        return spsc_queue->try_push(item);
    }

    bool queue_dequeue(spsc_queue_t* queue, void* item)
    {
        spsc::queue_t* spsc_queue = (spsc::queue_t*)queue;
        return spsc_queue->try_pop(item);
    }

} // namespace ncore