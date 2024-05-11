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
        static constexpr s32 c_cacheline_size = 64;
        static constexpr s32 c_item_size      = 8;

        struct slot_t
        {
            u64 m_item;
        };

        class queue_t
        {
        public:
            explicit queue_t(slot_t* array, u32 array_size)
                : m_slots(array)
                , m_capacity((array_size / c_item_size))
                , m_writeIdx(0)
                , m_readIdx(0)
            {
                static_assert(sizeof(queue_t) == c_cacheline_size, "queue_t should be a multiple of c_cacheline_size bytes");
            }

            inline bool try_push(u64 item)
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
                slot_t* dst = m_slots + writeIdx;
                dst->m_item = item;

                m_writeIdx = nextWriteIdx;
                return true;
            }

            bool try_pop(u64& item) noexcept
            {
                auto const readIdx = m_readIdx;
                if (readIdx == m_writeIdx)
                {
                    return false;
                }

                // retrieve item
                slot_t const* src = m_slots + readIdx;
                item              = src->m_item;

                auto nextReadIdx = readIdx + 1;
                if (nextReadIdx == m_capacity)
                {
                    nextReadIdx = 0;
                }
                m_readIdx = nextReadIdx;
                return true;
            }

            DCORE_CLASS_PLACEMENT_NEW_DELETE

            slot_t* m_slots;
            s32     m_writeIdx;
            s32     m_readIdx;
            s32     m_capacity;
            s32     m_dummy[16 - 5];
        };
    } // namespace local

    struct local_queue_t
    {
    };

    local_queue_t* local_queue_create(alloc_t* allocator, s32 item_count)
    {
        s32 const array_size = item_count * sizeof(local::slot_t);
        void*     mem        = allocator->allocate(sizeof(local::queue_t) + array_size, local::c_cacheline_size);
        if (mem == nullptr)
            return nullptr;
        local::slot_t* array_data = (local::slot_t*)((byte*)mem + sizeof(local::queue_t));
        ASSERTS(((u64)array_data & 0x7) == 0, "array_data is not aligned to 8 bytes");
        local::queue_t* queue = new (mem) local::queue_t(array_data, array_size);
        ASSERTS(((u64)queue & (local::c_cacheline_size)) == 0, "queue is not aligned to c_cacheline_size bytes");
        return (local_queue_t*)queue;
    }

    void queue_destroy(alloc_t* allocator, local_queue_t* queue)
    {
        local::queue_t* local_queue = (local::queue_t*)queue;
        allocator->deallocate(local_queue);
    }

    bool queue_enqueue(local_queue_t* queue, u64 item)
    {
        local::queue_t* local_queue = (local::queue_t*)queue;
        local_queue->try_push(item);
    }

    bool queue_dequeue(local_queue_t* queue, u64& item)
    {
        local::queue_t* local_queue = (local::queue_t*)queue;
        return local_queue->try_pop(item);
    }

} // namespace ncore