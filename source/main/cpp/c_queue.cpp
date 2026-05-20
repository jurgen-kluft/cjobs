#include "ccore/c_target.h"
#include "cbase/c_allocator.h"
#include "cbase/c_debug.h"
#include "cbase/c_integer.h"
#include "cjobs/c_queue.h"

#include <atomic>
#include <cassert>
#include <cstddef>

namespace ncore
{
    namespace queue_common
    {
        static constexpr u32 c_cacheline_size = 64;
    }

    namespace mpmc
    {
        struct slot_t
        {
            std::atomic<u32> turn;
            u32              padding0;
            u64              item;
            s64              padding[6];
        };

        class queue_t
        {
        public:
            explicit queue_t(slot_t* slots, u32 slot_count)
                : m_producer(slots, slot_count)
                , m_consumer(slots, slot_count)
            {
                STATIC_ASSERT(sizeof(slot_t) == queue_common::c_cacheline_size, "sizeof(slot_t) must be cache line size");
                STATIC_ASSERT(sizeof(header_t) == queue_common::c_cacheline_size, "sizeof(header_t) must be cache line size");
                STATIC_ASSERT(sizeof(queue_t) == 2 * queue_common::c_cacheline_size, "sizeof(queue_t) must be a multiple of cache line size to prevent false sharing between adjacent queues");
                STATIC_ASSERT(offsetof(queue_t, m_consumer.m_index) - offsetof(queue_t, m_producer.m_index) == static_cast<std::ptrdiff_t>(queue_common::c_cacheline_size), "head and tail must be a cache line apart to prevent false sharing");

                for (u32 i = 0; i < slot_count; ++i)
                {
                    slots[i].turn.store(0, std::memory_order_relaxed);
                }
            }

            DCORE_CLASS_PLACEMENT_NEW_DELETE

            queue_t(const queue_t&)            = delete;
            queue_t& operator=(const queue_t&) = delete;

            bool try_push(u64 item) noexcept
            {
                u32 head = m_producer.m_index.load(std::memory_order_relaxed);
                for (;;)
                {
                    slot_t*   slot          = m_producer.m_slots + m_producer.idx(head);
                    u32 const expected_turn = m_producer.turn(head) * 2;

                    if (expected_turn == slot->turn.load(std::memory_order_acquire))
                    {
                        if (m_producer.m_index.compare_exchange_weak(head, head + 1, std::memory_order_acq_rel, std::memory_order_relaxed))
                        {
                            slot->item = item;
                            slot->turn.store(expected_turn + 1, std::memory_order_release);
                            return true;
                        }
                    }
                    else
                    {
                        u32 const observed_head = m_producer.m_index.load(std::memory_order_acquire);
                        if (observed_head == head)
                            return false;
                        head = observed_head;
                    }
                }
            }

            bool try_pop(u64& item) noexcept
            {
                u32 tail = m_consumer.m_index.load(std::memory_order_relaxed);
                for (;;)
                {
                    slot_t*   slot          = m_consumer.m_slots + m_consumer.idx(tail);
                    u32 const expected_turn = m_consumer.turn(tail) * 2 + 1;
                    if (expected_turn == slot->turn.load(std::memory_order_acquire))
                    {
                        if (m_consumer.m_index.compare_exchange_weak(tail, tail + 1, std::memory_order_acq_rel, std::memory_order_relaxed))
                        {
                            item = slot->item;
                            slot->turn.store(expected_turn + 1, std::memory_order_release);
                            return true;
                        }
                    }
                    else
                    {
                        u32 const observed_tail = m_consumer.m_index.load(std::memory_order_acquire);
                        if (observed_tail == tail)
                            return false;
                        tail = observed_tail;
                    }
                }
            }

            struct header_t
            {
                header_t(slot_t* slots, u32 capacity)
                    : m_index(0)
                    , m_pad0(0)
                    , m_slots(slots)
                    , m_capacity(capacity)
                {
                }

                u32 idx(u32 i) const noexcept { return i % m_capacity; }
                u32 turn(u32 i) const noexcept { return i / m_capacity; }

                std::atomic<u32> m_index;
                u32 const        m_pad0;
                slot_t* const    m_slots;
                u32 const        m_capacity;
                u32              m_padding[11];
            };

            header_t m_producer;
            header_t m_consumer;
        };
    } // namespace mpmc

    struct mpmc_queue_t
    {
    };

    mpmc_queue_t* mpmc_queue_create(alloc_t* allocator, s32 item_count)
    {
        if (allocator == nullptr)
            return nullptr;

        u32 const requested_items = item_count > 0 ? (u32)item_count : 1u;
        u32 const slot_count      = requested_items + 1;
        u32 const array_size      = slot_count * sizeof(mpmc::slot_t);
        void*     mem             = allocator->allocate(sizeof(mpmc::queue_t) + array_size, queue_common::c_cacheline_size);
        if (mem == nullptr)
            return nullptr;

        mpmc::slot_t* array_data = (mpmc::slot_t*)((byte*)mem + sizeof(mpmc::queue_t));
        ASSERTS(g_ptr_is_aligned(array_data, queue_common::c_cacheline_size), "array must be aligned to cache line boundary to prevent false sharing");
        mpmc::queue_t* queue = new (mem) mpmc::queue_t(array_data, slot_count);
        return (mpmc_queue_t*)queue;
    }

    void queue_destroy(alloc_t* allocator, mpmc_queue_t* queue)
    {
        if (allocator == nullptr || queue == nullptr)
            return;

        allocator->deallocate((mpmc::queue_t*)queue);
    }

    bool queue_enqueue(mpmc_queue_t* queue, u64 item)
    {
        if (queue == nullptr)
            return false;

        return ((mpmc::queue_t*)queue)->try_push(item);
    }

    bool queue_dequeue(mpmc_queue_t* queue, u64& item)
    {
        if (queue == nullptr)
            return false;

        return ((mpmc::queue_t*)queue)->try_pop(item);
    }

    namespace spmc
    {
        struct slot_t
        {
            std::atomic<u32> turn;
            u32              padding0;
            u64              item;
            s64              padding[6];
        };

        class queue_t
        {
        public:
            explicit queue_t(slot_t* slots, u32 slot_count)
                : m_producer(slots, slot_count)
                , m_consumer(slots, slot_count)
            {
                STATIC_ASSERT(sizeof(slot_t) == queue_common::c_cacheline_size, "sizeof(slot_t) must be cache line size");
                STATIC_ASSERT(sizeof(header_t) == queue_common::c_cacheline_size, "sizeof(header_t) must be cache line size");
                STATIC_ASSERT(sizeof(queue_t) == 2 * queue_common::c_cacheline_size, "sizeof(queue_t) must be a multiple of cache line size to prevent false sharing between adjacent queues");
                STATIC_ASSERT(offsetof(queue_t, m_consumer.m_index) - offsetof(queue_t, m_producer.m_index) == static_cast<std::ptrdiff_t>(queue_common::c_cacheline_size), "head and tail must be a cache line apart to prevent false sharing");

                for (u32 i = 0; i < slot_count; ++i)
                {
                    slots[i].turn.store(0, std::memory_order_relaxed);
                }
            }

            DCORE_CLASS_PLACEMENT_NEW_DELETE

            queue_t(const queue_t&)            = delete;
            queue_t& operator=(const queue_t&) = delete;

            bool try_push(u64 item) noexcept
            {
                u32 const head          = m_producer.m_index.load(std::memory_order_relaxed);
                slot_t&   slot          = *(m_producer.m_slots + m_producer.idx(head));
                u32 const expected_turn = m_producer.turn(head) * 2;
                if (expected_turn != slot.turn.load(std::memory_order_acquire))
                    return false;

                slot.item = item;
                slot.turn.store(expected_turn + 1, std::memory_order_release);
                m_producer.m_index.store(head + 1, std::memory_order_release);
                return true;
            }

            bool try_pop(u64& item) noexcept
            {
                u32 tail = m_consumer.m_index.load(std::memory_order_relaxed);
                for (;;)
                {
                    slot_t&   slot          = *(m_consumer.m_slots + m_consumer.idx(tail));
                    u32 const expected_turn = m_consumer.turn(tail) * 2 + 1;
                    if (expected_turn == slot.turn.load(std::memory_order_acquire))
                    {
                        if (m_consumer.m_index.compare_exchange_weak(tail, tail + 1, std::memory_order_acq_rel, std::memory_order_relaxed))
                        {
                            item = slot.item;
                            slot.turn.store(expected_turn + 1, std::memory_order_release);
                            return true;
                        }
                    }
                    else
                    {
                        u32 const observed_tail = m_consumer.m_index.load(std::memory_order_acquire);
                        if (observed_tail == tail)
                            return false;
                        tail = observed_tail;
                    }
                }
            }

            struct header_t
            {
                header_t(slot_t* slots, u32 capacity)
                    : m_index(0)
                    , m_pad0(0)
                    , m_slots(slots)
                    , m_capacity(capacity)
                {
                }

                u32 idx(u32 i) const noexcept { return i % m_capacity; }
                u32 turn(u32 i) const noexcept { return i / m_capacity; }

                std::atomic<u32> m_index;
                u32 const        m_pad0;
                slot_t* const    m_slots;
                u32 const        m_capacity;
                u32              m_padding[11];
            };

            header_t m_producer;
            header_t m_consumer;
        };
    } // namespace spmc

    struct spmc_queue_t
    {
    };

    spmc_queue_t* spmc_queue_create(alloc_t* allocator, s32 item_count)
    {
        if (allocator == nullptr)
            return nullptr;

        u32 const requested_items = item_count > 0 ? (u32)item_count : 1u;
        u32 const slot_count      = requested_items + 1;
        u32 const array_size      = slot_count * sizeof(spmc::slot_t);
        void*     mem             = allocator->allocate(sizeof(spmc::queue_t) + array_size, queue_common::c_cacheline_size);
        if (mem == nullptr)
            return nullptr;

        spmc::slot_t* array_data = (spmc::slot_t*)((byte*)mem + sizeof(spmc::queue_t));
        ASSERTS(g_ptr_is_aligned(array_data, queue_common::c_cacheline_size), "array must be aligned to cache line boundary to prevent false sharing");
        spmc::queue_t* queue = new (mem) spmc::queue_t(array_data, slot_count);
        return (spmc_queue_t*)queue;
    }

    void queue_destroy(alloc_t* allocator, spmc_queue_t* queue)
    {
        if (allocator == nullptr || queue == nullptr)
            return;

        allocator->deallocate((spmc::queue_t*)queue);
    }

    bool queue_enqueue(spmc_queue_t* queue, u64 item)
    {
        if (queue == nullptr)
            return false;

        return ((spmc::queue_t*)queue)->try_push(item);
    }

    bool queue_dequeue(spmc_queue_t* queue, u64& item)
    {
        if (queue == nullptr)
            return false;

        return ((spmc::queue_t*)queue)->try_pop(item);
    }

    namespace spsc
    {
        class queue_t
        {
        public:
            explicit queue_t(void* array, u32 slot_count)
                : m_writeIdx(0)
                , m_producer((u64*)array, slot_count)
                , m_readIdxCache(0)
                , m_readIdx(0)
                , m_consumer((u64*)array, slot_count)
                , m_writeIdxCache(0)
            {
                STATIC_ASSERT(alignof(queue_t) == queue_common::c_cacheline_size, "");
                STATIC_ASSERT(sizeof(queue_t) == 2 * queue_common::c_cacheline_size, "");
                assert(reinterpret_cast<char*>(&m_readIdx) - reinterpret_cast<char*>(&m_writeIdx) >= static_cast<std::ptrdiff_t>(queue_common::c_cacheline_size));
            }

            queue_t(const queue_t&)            = delete;
            queue_t& operator=(const queue_t&) = delete;

            bool try_push(u64 item)
            {
                u32 const writeIdx     = m_writeIdx.load(std::memory_order_relaxed);
                u32       nextWriteIdx = writeIdx + 1;
                if (nextWriteIdx == m_producer.m_capacity)
                    nextWriteIdx = 0;

                if (nextWriteIdx == m_readIdxCache)
                {
                    m_readIdxCache = m_readIdx.load(std::memory_order_acquire);
                    if (nextWriteIdx == m_readIdxCache)
                        return false;
                }

                m_producer.m_slots[writeIdx] = item;
                m_writeIdx.store(nextWriteIdx, std::memory_order_release);
                return true;
            }

            bool try_pop(u64& item) noexcept
            {
                u32 const readIdx = m_readIdx.load(std::memory_order_relaxed);
                if (readIdx == m_writeIdxCache)
                {
                    m_writeIdxCache = m_writeIdx.load(std::memory_order_acquire);
                    if (m_writeIdxCache == readIdx)
                        return false;
                }

                item = m_consumer.m_slots[readIdx];

                u32 nextReadIdx = readIdx + 1;
                if (nextReadIdx == m_consumer.m_capacity)
                    nextReadIdx = 0;

                m_readIdx.store(nextReadIdx, std::memory_order_release);
                return true;
            }

            s32 try_pop_multiple(u64* items, s32 count) noexcept
            {
                u32 readIdx = m_readIdx.load(std::memory_order_relaxed);
                if (readIdx == m_writeIdxCache)
                {
                    m_writeIdxCache = m_writeIdx.load(std::memory_order_acquire);
                    if (m_writeIdxCache == readIdx)
                        return 0;
                }

                s32 i = 0;
                while (i < count && readIdx != m_writeIdxCache)
                {
                    items[i++] = m_consumer.m_slots[readIdx++];
                    if (readIdx == m_consumer.m_capacity)
                        readIdx = 0;
                }

                m_readIdx.store(readIdx, std::memory_order_release);
                return i;
            }

            DCORE_CLASS_PLACEMENT_NEW_DELETE

            struct header_t
            {
                header_t(u64* slots, u32 capacity)
                    : m_slots(slots)
                    , m_capacity(capacity)
                {
                }

                u64* const m_slots;
                u32 const  m_capacity;
            };

            alignas(queue_common::c_cacheline_size) std::atomic<u32> m_writeIdx;
            u32      m_readIdxCache;
            header_t m_producer;

            alignas(queue_common::c_cacheline_size) std::atomic<u32> m_readIdx;
            u32      m_writeIdxCache;
            header_t m_consumer;
        };
    } // namespace spsc

    struct spsc_queue_t
    {
    };

    spsc_queue_t* spsc_queue_create(alloc_t* allocator, s32 item_count)
    {
        if (allocator == nullptr)
            return nullptr;

        u32 const requested_items = item_count > 0 ? (u32)item_count : 1u;
        u32 const slot_count      = requested_items + 1;
        u32 const array_size      = slot_count * sizeof(u64);
        void*     mem             = allocator->allocate(array_size + sizeof(spsc::queue_t), queue_common::c_cacheline_size);
        if (mem == nullptr)
            return nullptr;

        void*          array_data = ((byte*)mem + sizeof(spsc::queue_t));
        spsc::queue_t* queue      = new (mem) spsc::queue_t(array_data, slot_count);
        return (spsc_queue_t*)queue;
    }

    void queue_destroy(alloc_t* allocator, spsc_queue_t* queue)
    {
        if (allocator == nullptr || queue == nullptr)
            return;

        allocator->deallocate((spsc::queue_t*)queue);
    }

    bool queue_enqueue(spsc_queue_t* queue, u64 item)
    {
        if (queue == nullptr)
            return false;

        return ((spsc::queue_t*)queue)->try_push(item);
    }

    bool queue_dequeue(spsc_queue_t* queue, u64& item)
    {
        if (queue == nullptr)
            return false;

        return ((spsc::queue_t*)queue)->try_pop(item);
    }

    s32 queue_dequeue_multiple(spsc_queue_t* queue, u64* items, s32 count)
    {
        if (queue == nullptr || items == nullptr || count <= 0)
            return 0;

        return ((spsc::queue_t*)queue)->try_pop_multiple(items, count);
    }

    namespace mpsc
    {
        static constexpr u32 c_max_producers = 64;

        struct local_ring_t
        {
            u64* m_slots;
            u32  m_capacity;
            u32  m_writer;
            u32  m_reader;

            local_ring_t()
                : m_slots(nullptr)
                , m_capacity(0)
                , m_writer(0)
                , m_reader(0)
            {
            }

            void setup(u64* slots, u32 capacity)
            {
                m_slots    = slots;
                m_capacity = capacity;
                m_writer   = 0;
                m_reader   = 0;
            }

            u32 next(u32 value) const
            {
                ++value;
                if (value == m_capacity)
                    value = 0;
                return value;
            }

            bool is_empty() const { return m_reader == m_writer; }
            bool is_full() const { return next(m_writer) == m_reader; }

            bool add(u64 item)
            {
                if (is_full())
                    return false;

                m_slots[m_writer] = item;
                m_writer          = next(m_writer);
                return true;
            }

            bool add_multiple(const u64* items, u32 count)
            {
                for (u32 i = 0; i < count; ++i)
                {
                    if (!add(items[i]))
                        return false;
                }
                return true;
            }

            bool inspect(u32& begin, u32& end) const
            {
                if (is_empty())
                    return false;
                begin = m_reader;
                end   = m_writer;
                return true;
            }

            u64 read(u32& idx) const
            {
                u32 const read_idx = idx;
                idx                = next(idx);
                return m_slots[read_idx];
            }

            void release(u32 idx) { m_reader = idx; }

            DCORE_CLASS_PLACEMENT_NEW_DELETE
        };

        static local_ring_t* create_ring_buffer(alloc_t* allocator, u32 capacity)
        {
            u32 const size   = sizeof(local_ring_t) + sizeof(u64) * capacity;
            u8*       buffer = (u8*)allocator->allocate(size);
            if (buffer == nullptr)
                return nullptr;

            local_ring_t* ring = new (buffer) local_ring_t();
            ring->setup((u64*)(buffer + sizeof(local_ring_t)), capacity);
            return ring;
        }

        struct queue_t
        {
            queue_t()
                : m_queues_state(0)
                , m_num_queues(0)
                , m_producer_queues(nullptr)
                , m_consumer_ring(nullptr)
            {
            }

            alignas(queue_common::c_cacheline_size) std::atomic<u64> m_queues_state;
            u32            m_num_queues;
            spsc_queue_t** m_producer_queues;
            local_ring_t*  m_consumer_ring;

            bool push(s32 producerIdx, u64 item)
            {
                if (producerIdx < 0 || (u32)producerIdx >= m_num_queues)
                    return false;

                spsc_queue_t* queue = m_producer_queues[producerIdx];
                if (!queue_enqueue(queue, item))
                    return false;

                m_queues_state.fetch_or(1ull << producerIdx, std::memory_order_release);
                return true;
            }

            bool inspect(u32& begin, u32& end)
            {
                if (m_consumer_ring->inspect(begin, end))
                    return true;

                u64 const pending_state = m_queues_state.exchange(0, std::memory_order_acquire);
                if (pending_state == 0)
                    return false;

                u64       items[8];
                s32 const max_items = (s32)(sizeof(items) / sizeof(items[0]));
                for (s32 i = 0; i < (s32)m_num_queues; ++i)
                {
                    spsc_queue_t* queue = m_producer_queues[i];
                    while (true)
                    {
                        s32 const num_dequeued = queue_dequeue_multiple(queue, items, max_items);
                        if (num_dequeued == 0)
                            break;
                        if (!m_consumer_ring->add_multiple(items, (u32)num_dequeued))
                        {
                            ASSERT(false);
                            return false;
                        }
                    }
                }
                return m_consumer_ring->inspect(begin, end);
            }

            u64 read(u32& idx) const { return m_consumer_ring->read(idx); }

            s8 release(u32 idx, u32 end)
            {
                ASSERT(idx == end);
                m_consumer_ring->release(end);

                s8 result = m_consumer_ring->is_empty() ? 0 : 1;
                if (result == 0)
                {
                    u64 const state = m_queues_state.load(std::memory_order_acquire);
                    result          = state == 0 ? 0 : 1;
                }
                return result;
            }

            DCORE_CLASS_PLACEMENT_NEW_DELETE
        };

        static void destroy_queue(alloc_t* allocator, queue_t* queue)
        {
            if (allocator == nullptr || queue == nullptr)
                return;

            if (queue->m_producer_queues != nullptr)
            {
                for (u32 i = 0; i < queue->m_num_queues; ++i)
                {
                    if (queue->m_producer_queues[i] != nullptr)
                        queue_destroy(allocator, queue->m_producer_queues[i]);
                }
                allocator->deallocate(queue->m_producer_queues);
            }

            if (queue->m_consumer_ring != nullptr)
                allocator->deallocate(queue->m_consumer_ring);

            allocator->deallocate(queue);
        }

        static queue_t* create_queue(alloc_t* allocator, s32 producer_count, s32 item_count)
        {
            if (allocator == nullptr || producer_count <= 0 || producer_count > (s32)c_max_producers)
                return nullptr;

            queue_t* queue = g_allocate<queue_t>(allocator);
            if (queue == nullptr)
                return nullptr;

            queue->m_num_queues      = (u32)producer_count;
            queue->m_producer_queues = (spsc_queue_t**)allocator->allocate(producer_count * sizeof(spsc_queue_t*));
            if (queue->m_producer_queues == nullptr)
            {
                allocator->deallocate(queue);
                return nullptr;
            }

            for (s32 i = 0; i < producer_count; ++i)
                queue->m_producer_queues[i] = nullptr;

            u32 const producer_items = item_count > 0 ? (u32)item_count : 1u;
            queue->m_consumer_ring   = create_ring_buffer(allocator, (u32)producer_count * producer_items + 1u);
            if (queue->m_consumer_ring == nullptr)
            {
                allocator->deallocate(queue->m_producer_queues);
                allocator->deallocate(queue);
                return nullptr;
            }

            queue->m_queues_state.store(0, std::memory_order_relaxed);
            for (s32 i = 0; i < producer_count; ++i)
            {
                queue->m_producer_queues[i] = spsc_queue_create(allocator, item_count);
                if (queue->m_producer_queues[i] == nullptr)
                {
                    destroy_queue(allocator, queue);
                    return nullptr;
                }
            }
            return queue;
        }
    } // namespace mpsc

    struct mpsc_queue_t
    {
    };

    mpsc_queue_t* mpsc_queue_create(alloc_t* allocator, s32 producer_count, s32 item_count) { return (mpsc_queue_t*)mpsc::create_queue(allocator, producer_count, item_count); }

    void queue_destroy(alloc_t* allocator, mpsc_queue_t* queue) { mpsc::destroy_queue(allocator, (mpsc::queue_t*)queue); }

    bool queue_enqueue(mpsc_queue_t* queue, s32 producerIdx, u64 item)
    {
        if (queue == nullptr)
            return false;

        return ((mpsc::queue_t*)queue)->push(producerIdx, item);
    }

    bool queue_inspect(mpsc_queue_t* queue, u32& begin, u32& end)
    {
        if (queue == nullptr)
            return false;

        return ((mpsc::queue_t*)queue)->inspect(begin, end);
    }

    bool queue_dequeue(mpsc_queue_t* queue, u32& idx, u32 end, u64& item)
    {
        if (queue == nullptr || idx == end)
            return false;

        item = ((mpsc::queue_t*)queue)->read(idx);
        return true;
    }

    s8 queue_release(mpsc_queue_t* queue, u32 idx, u32 end)
    {
        if (queue == nullptr)
            return 0;

        return ((mpsc::queue_t*)queue)->release(idx, end);
    }

    namespace local
    {
        struct slot_t
        {
            u64 m_item;
        };

        class queue_t
        {
        public:
            explicit queue_t(slot_t* array, u32 item_count)
                : m_slots(array)
                , m_writeIdx(0)
                , m_readIdx(0)
                , m_capacity(item_count)
            {
            }

            bool try_push(u64 item)
            {
                s32 const writeIdx     = m_writeIdx;
                s32       nextWriteIdx = writeIdx + 1;
                if (nextWriteIdx == m_capacity)
                    nextWriteIdx = 0;
                if (nextWriteIdx == m_readIdx)
                    return false;

                m_slots[writeIdx].m_item = item;
                m_writeIdx               = nextWriteIdx;
                return true;
            }

            bool try_pop(u64& item) noexcept
            {
                s32 const readIdx = m_readIdx;
                if (readIdx == m_writeIdx)
                    return false;

                item = m_slots[readIdx].m_item;

                s32 nextReadIdx = readIdx + 1;
                if (nextReadIdx == m_capacity)
                    nextReadIdx = 0;
                m_readIdx = nextReadIdx;
                return true;
            }

            DCORE_CLASS_PLACEMENT_NEW_DELETE

            slot_t* m_slots;
            s32     m_writeIdx;
            s32     m_readIdx;
            s32     m_capacity;
            s32     m_dummy;
        };
    } // namespace local

    struct local_queue_t
    {
    };

    local_queue_t* local_queue_create(alloc_t* allocator, s32 item_count)
    {
        if (allocator == nullptr || item_count <= 0)
            return nullptr;

        s32 const array_size = item_count * sizeof(local::slot_t);
        void*     mem        = allocator->allocate(sizeof(local::queue_t) + array_size, sizeof(void*));
        if (mem == nullptr)
            return nullptr;

        local::slot_t* array_data = (local::slot_t*)((byte*)mem + sizeof(local::queue_t));
        ASSERTS(((u64)array_data & 0x7) == 0, "array_data is not aligned to 8 bytes");
        local::queue_t* queue = new (mem) local::queue_t(array_data, item_count);
        return (local_queue_t*)queue;
    }

    void queue_destroy(alloc_t* allocator, local_queue_t* queue)
    {
        if (allocator == nullptr || queue == nullptr)
            return;

        allocator->deallocate((local::queue_t*)queue);
    }

    bool queue_enqueue(local_queue_t* queue, u64 item)
    {
        if (queue == nullptr)
            return false;

        return ((local::queue_t*)queue)->try_push(item);
    }

    bool queue_dequeue(local_queue_t* queue, u64& item)
    {
        if (queue == nullptr)
            return false;

        return ((local::queue_t*)queue)->try_pop(item);
    }

} // namespace ncore
