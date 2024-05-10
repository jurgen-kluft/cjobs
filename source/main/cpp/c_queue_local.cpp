#include "ccore/c_target.h"
#include "cbase/c_allocator.h"
#include "cbase/c_integer.h"
#include "cjobs/c_queue.h"

#include <atomic>
#include <cassert>
#include <stdexcept>

namespace ncore
{
    namespace local
    {
        static constexpr s32 size_alignment = 8;

        class queue_t
        {
        public:
            explicit queue_t(void* array, u32 array_size, u16 item_size)
                : m_item_size(item_size)
                , m_slot_size(((item_size + size_alignment - 1) / size_alignment) * size_alignment)
                , m_slots((byte*)array)
                , m_capacity((array_size / m_slot_size))
                , m_writeIdx(0)
                , m_readIdx(0)
            {
                static_assert(alignof(queue_t) == size_alignment, "");
            }

            inline bool try_push(void* item)
            {
                auto const writeIdx     = m_writeIdx;
                auto       nextWriteIdx = writeIdx + 1;
                if (nextWriteIdx == m_capacity)
                {
                    nextWriteIdx = 0;
                }
                if (nextWriteIdx == m_readIdx)
                {
                    return false;
                }

                // store item
                byte*             dst = m_slots + (writeIdx * m_slot_size);
                byte const* const end = dst + m_item_size;
                byte const*       src = (byte const*)item;
                while (dst < end)
                    *dst++ = *src++;

                m_writeIdx = nextWriteIdx;
                return true;
            }

            bool try_pop(void* item) noexcept
            {
                auto const readIdx = m_readIdx;
                if (readIdx == m_writeIdx)
                {
                    return false;
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
                m_readIdx = nextReadIdx;
                return true;
            }

            DCORE_CLASS_PLACEMENT_NEW_DELETE

            byte* m_slots;
            s32   m_writeIdx;
            s32   m_readIdx;
            u16   m_item_size;
            u16   m_slot_size;
            s32   m_capacity;
        };
    } // namespace local

    struct local_queue_t
    {
    };

    local_queue_t* local_queue_create(alloc_t* allocator, s32 item_count, s32 item_size)
    {
        s32 const array_size = item_count * math::alignUp(item_size, local::size_alignment);
        void*     mem        = allocator->allocate(array_size + sizeof(local::queue_t), local::size_alignment);
        if (mem == nullptr)
            return nullptr;
        void*           array_data = ((byte*)mem + sizeof(local::queue_t));
        local::queue_t* queue      = new (mem) local::queue_t(array_data, array_size, item_size);
        return (local_queue_t*)queue;
    }

    void queue_destroy(alloc_t* allocator, local_queue_t* queue)
    {
        local::queue_t* local_queue = (local::queue_t*)queue;
        allocator->deallocate(local_queue);
    }

    bool queue_enqueue(local_queue_t* queue, void* item)
    {
        local::queue_t* local_queue = (local::queue_t*)queue;
        local_queue->try_push(item);
    }

    bool queue_dequeue(local_queue_t* queue, void* item)
    {
        local::queue_t* local_queue = (local::queue_t*)queue;
        return local_queue->try_pop(item);
    }

} // namespace ncore