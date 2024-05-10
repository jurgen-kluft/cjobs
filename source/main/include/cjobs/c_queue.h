#ifndef __CJOBS_QUEUE_H__
#define __CJOBS_QUEUE_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    class alloc_t;

    // Example:
    //    mpmc_queue_t* queue = mpmc_queue_create(allocator, 10, sizeof(int));
    //    int itemA = 0;
    //    int itemB = 1;
    //    mpmc_queue_enqueue(queue, &itemA);
    //    mpmc_queue_enqueue(queue, &itemB);
    //
    //    int item;
    //    mpmc_queue_dequeue(queue, &item);
    //    mpmc_queue_dequeue(queue, &item);
    //    mpmc_queue_destroy(queue);

    struct mpmc_queue_t;
    mpmc_queue_t* mpmc_queue_create(alloc_t* allocator, s32 item_count, s32 item_size);
    void          queue_destroy(alloc_t* allocator, mpmc_queue_t* queue);
    bool          queue_enqueue(mpmc_queue_t* queue, void* item);
    bool          queue_dequeue(mpmc_queue_t* queue, void* item);

    // template <typename T> class mpmc_queue_t
    // {
    //     void* m_queue;
    // public:
    //     mpmc_queue_t()
    //         : m_queue(nullptr)
    //     {
    //     }

    //     void setup(alloc_t* allocator, s32 item_count) { m_queue = mpmc_queue_create(allocator, item_count, sizeof(T)); }
    //     void teardown(alloc_t* allocator) { queue_destroy(allocator, m_queue); }
    //     bool enqueue(T const& item) { return queue_enqueue(m_queue, &item); }
    //     bool dequeue(T& item) { return queue_dequeue(m_queue, &item); }
    // };

    struct spsc_queue_t;
    spsc_queue_t* spsc_queue_create(alloc_t* allocator, s32 item_count, s32 item_size);
    void          queue_destroy(alloc_t* allocator, spsc_queue_t* queue);
    bool          queue_enqueue(spsc_queue_t* queue, void* item);
    bool          queue_dequeue(spsc_queue_t* queue, void* item);

    struct mpsc_queue_t;
    mpsc_queue_t* mpsc_queue_create(alloc_t* allocator, s32 item_count, s32 item_size);
    void          queue_destroy(alloc_t* allocator, mpsc_queue_t* queue);
    bool          queue_enqueue(mpsc_queue_t* queue, void* item);
    bool          queue_dequeue(mpsc_queue_t* queue, void* item);

    struct spmc_queue_t;
    spmc_queue_t* spmc_queue_create(alloc_t* allocator, s32 item_count, s32 item_size);
    void          queue_destroy(alloc_t* allocator, spmc_queue_t* queue);
    bool          queue_enqueue(spmc_queue_t* queue, void* item);
    bool          queue_dequeue(spmc_queue_t* queue, void* item);

    struct local_queue_t;
    local_queue_t* local_queue_create(alloc_t* allocator, s32 item_count, s32 item_size);
    void           queue_destroy(alloc_t* allocator, local_queue_t* queue);
    bool           queue_enqueue(local_queue_t* queue, void* item);
    bool           queue_dequeue(local_queue_t* queue, void* item);

} // namespace ncore

#endif // __CJOBS_QUEUE_H__
