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
            explicit queue_t(void* array, u32 array_size, u32 item_size)
                : m_item_size(item_size)
                , m_slot_size(((item_size + cacheline_size - 1) / cacheline_size) * cacheline_size)
                , m_slots((byte*)array)
                , m_capacity((array_size / m_slot_size) - 1) // Note: We need one additional (dummy) slot to prevent false sharing on the last slot
                , m_writeIdx(0)
                , m_readIdxCache(0)
                , m_readIdx(0)
                , m_writeIdxCache(0)
            {
                static_assert(alignof(queue_t) == cacheline_size, "");
                static_assert(sizeof(queue_t) >= 3 * cacheline_size, "");
                assert(reinterpret_cast<char*>(&m_readIdx) - reinterpret_cast<char*>(&m_writeIdx) >= static_cast<std::ptrdiff_t>(cacheline_size));
            }

            // non-copyable and non-movable
            queue_t(const queue_t&)            = delete;
            queue_t& operator=(const queue_t&) = delete;

            void push(void* item)
            {
                auto const writeIdx     = m_writeIdx.load(std::memory_order_relaxed);
                auto       nextWriteIdx = writeIdx + 1;
                if (nextWriteIdx == m_capacity)
                {
                    nextWriteIdx = 0;
                }
                while (nextWriteIdx == m_readIdxCache)
                {
                    m_readIdxCache = m_readIdx.load(std::memory_order_acquire);
                }

                // store item
                byte*             dst = m_slots + (writeIdx * m_slot_size);
                byte const* const end = dst + m_item_size;
                byte const*       src = (byte const*)item;
                while (dst < end)
                    *dst++ = *src++;

                m_writeIdx.store(nextWriteIdx, std::memory_order_release);
            }

            inline bool try_push(void* item)
            {
                auto const writeIdx     = m_writeIdx.load(std::memory_order_relaxed);
                auto       nextWriteIdx = writeIdx + 1;
                if (nextWriteIdx == m_capacity)
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
                byte*             dst = m_slots + (writeIdx * m_slot_size);
                byte const* const end = dst + m_item_size;
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
                byte const*       src = m_slots + (readIdx * m_slot_size);
                byte const* const end = src + m_item_size;
                byte*             dst = (byte*)item;
                while (src < end)
                    *dst++ = *src++;

                auto nextReadIdx = readIdx + 1;
                if (nextReadIdx == m_capacity)
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
                    diff += m_capacity;
                }
                return static_cast<s32>(diff);
            }

            inline bool empty() const noexcept { return m_writeIdx.load(std::memory_order_acquire) == m_readIdx.load(std::memory_order_acquire); }
            inline s32  capacity() const noexcept { return m_capacity; }

        private:
            byte* m_slots;
            s32   m_item_size;
            s32   m_slot_size;
            s32   m_capacity;

            // Align to cache line size in order to avoid false sharing
            // m_readIdxCache and m_writeIdxCache is used to reduce the amount of cache
            // coherency traffic
            alignas(cacheline_size) std::atomic<s32> m_writeIdx;
            alignas(cacheline_size) s32 m_readIdxCache;
            alignas(cacheline_size) std::atomic<s32> m_readIdx;
            alignas(cacheline_size) s32 m_writeIdxCache;
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