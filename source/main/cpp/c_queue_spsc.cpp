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
        static constexpr int_t c_cacheline_size = 64; // std::hardware_destructive_interference_size;
        static constexpr u32   c_item_size      = 8;

        class queue_t
        {
        public:
            // Note: We need one additional (dummy) slot to prevent false sharing on the last slot
            explicit queue_t(void* array, u32 array_size)
                : m_writeIdx(0)
                , m_producer((u64*)array, array_size)
                , m_readIdxCache(0)
                , m_readIdx(0)
                , m_consumer((u64*)array, array_size)
                , m_writeIdxCache(0)
            {
                static_assert(alignof(queue_t) == c_cacheline_size, "");
                static_assert(sizeof(queue_t) == 2 * c_cacheline_size, "");
                assert(reinterpret_cast<char*>(&m_readIdx) - reinterpret_cast<char*>(&m_writeIdx) >= static_cast<std::ptrdiff_t>(c_cacheline_size));
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
                u64* dst = m_producer.m_slots + writeIdx;
                *dst     = (u64)item;

                m_writeIdx.store(nextWriteIdx, std::memory_order_release);
            }

            inline bool try_push(u64 item)
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
                u64* dst = m_producer.m_slots + writeIdx;
                *dst     = item;

                m_writeIdx.store(nextWriteIdx, std::memory_order_release);
                return true;
            }

            bool try_pop(u64& item) noexcept
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
                u64 const* src = m_consumer.m_slots + readIdx;
                item           = *src;

                auto nextReadIdx = readIdx + 1;
                if (nextReadIdx == m_consumer.m_capacity)
                {
                    nextReadIdx = 0;
                }
                m_readIdx.store(nextReadIdx, std::memory_order_release);

                return true;
            }

            DCORE_CLASS_PLACEMENT_NEW_DELETE

            struct header_t
            {
                header_t(u64* slots, s32 capacity)
                    : m_slots(slots)
                    , m_capacity((capacity / c_item_size) - 1)
                {
                }

                u64* const m_slots;
                s32 const  m_capacity;
            };

            // Align to cache line size in order to avoid false sharing
            // m_readIdxCache and m_writeIdxCache is used to reduce the amount of cache coherency traffic

            // Producer
            alignas(c_cacheline_size) std::atomic<s32> m_writeIdx;
            s32      m_readIdxCache;
            header_t m_producer;

            // Consumer
            alignas(c_cacheline_size) std::atomic<s32> m_readIdx;
            s32      m_writeIdxCache;
            header_t m_consumer;
        };
    } // namespace spsc

    struct spsc_queue_t
    {
    };

    spsc_queue_t* spsc_queue_create(alloc_t* allocator, s32 item_count)
    {
        s32 const array_size = (item_count + 1) * math::alignUp(spsc::c_item_size, spsc::c_cacheline_size);
        void*     mem        = allocator->allocate(array_size + sizeof(spsc::queue_t), spsc::c_cacheline_size);
        if (mem == nullptr)
            return nullptr;
        void*          array_data = ((byte*)mem + sizeof(spsc::queue_t));
        spsc::queue_t* queue      = new (mem) spsc::queue_t(array_data, array_size);
        return (spsc_queue_t*)queue;
    }

    void queue_destroy(alloc_t* allocator, spsc_queue_t* queue)
    {
        spsc::queue_t* spsc_queue = (spsc::queue_t*)queue;
        allocator->deallocate(spsc_queue);
    }

    bool queue_enqueue(spsc_queue_t* queue, u64 item)
    {
        spsc::queue_t* spsc_queue = (spsc::queue_t*)queue;
        return spsc_queue->try_push(item);
    }

    bool queue_dequeue(spsc_queue_t* queue, u64& item)
    {
        spsc::queue_t* spsc_queue = (spsc::queue_t*)queue;
        return spsc_queue->try_pop(item);
    }

} // namespace ncore