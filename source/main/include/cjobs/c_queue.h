#ifndef __CJOBS_QUEUE_H__
#define __CJOBS_QUEUE_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    class alloc_t;

    // Definition of the queue types:
    // Multi-Producer/Multi-Consumer (MPMC)
    // Single-Producer/Multi-Consumer (SPMC)
    // Multi-Producer/Single-Consumer (MPSC)
    // Single-Producer/Single-Consumer (SPSC)
    //
    // Example:
    //    mpmc_queue_t* queue = mpmc_queue_create(allocator, 10);
    //    u64 itemA = 0;
    //    u64 itemB = 1;
    //    queue_enqueue(queue, itemA);
    //    queue_enqueue(queue, itemB);
    //
    //    u64 item;
    //    queue_dequeue(queue, item);
    //    queue_dequeue(queue, item);
    //    queue_destroy(queue);

    struct mpmc_queue_t;
    mpmc_queue_t* mpmc_queue_create(alloc_t* allocator, s32 item_count);
    void          queue_destroy(alloc_t* allocator, mpmc_queue_t* queue);
    bool          queue_enqueue(mpmc_queue_t* queue, u64 item);
    bool          queue_dequeue(mpmc_queue_t* queue, u64& item);

    struct spsc_queue_t;
    spsc_queue_t* spsc_queue_create(alloc_t* allocator, s32 item_count);
    void          queue_destroy(alloc_t* allocator, spsc_queue_t* queue);
    bool          queue_enqueue(spsc_queue_t* queue, u64 item);
    bool          queue_dequeue(spsc_queue_t* queue, u64& item);

    struct mpsc_queue_t;
    mpsc_queue_t* mpsc_queue_create(alloc_t* allocator, s32 item_count);
    void          queue_destroy(alloc_t* allocator, mpsc_queue_t* queue);
    bool          queue_enqueue(mpsc_queue_t* queue, u64 item);
    bool          queue_dequeue(mpsc_queue_t* queue, u64& item);

    struct spmc_queue_t;
    spmc_queue_t* spmc_queue_create(alloc_t* allocator, s32 item_count);
    void          queue_destroy(alloc_t* allocator, spmc_queue_t* queue);
    bool          queue_enqueue(spmc_queue_t* queue, u64 item);
    bool          queue_dequeue(spmc_queue_t* queue, u64& item);

    struct local_queue_t;
    local_queue_t* local_queue_create(alloc_t* allocator, s32 item_count);
    void           queue_destroy(alloc_t* allocator, local_queue_t* queue);
    bool           queue_enqueue(local_queue_t* queue, u64 item);
    bool           queue_dequeue(local_queue_t* queue, u64& item);

    // Blocking versions
    struct mpsc_blocking_queue_t;
    mpsc_blocking_queue_t* mpsc_blocking_queue_create(alloc_t* allocator, s32 item_count);
    void                   queue_destroy(alloc_t* allocator, mpsc_blocking_queue_t* queue);
    bool                   queue_enqueue(mpsc_blocking_queue_t* queue, u64 item);
    bool                   queue_dequeue(mpsc_blocking_queue_t* queue, u64& item);

} // namespace ncore

#endif // __CJOBS_QUEUE_H__
