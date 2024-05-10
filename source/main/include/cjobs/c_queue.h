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
    void          mpmc_queue_destroy(alloc_t* allocator, mpmc_queue_t* queue);
    void          mpmc_queue_enqueue(mpmc_queue_t* queue, void* item);
    bool          mpmc_queue_dequeue(mpmc_queue_t* queue, void* item);

    struct spsc_queue_t;
    spsc_queue_t* spsc_queue_create(alloc_t* allocator, s32 item_count, s32 item_size);
    void          spsc_queue_destroy(alloc_t* allocator, spsc_queue_t* queue);
    void          spsc_queue_enqueue(spsc_queue_t* queue, void* item);
    bool          spsc_queue_dequeue(spsc_queue_t* queue, void* item);

    struct mpsc_queue_t;
    mpsc_queue_t* mpsc_queue_create(alloc_t* allocator, s32 item_count, s32 item_size);
    void          mpsc_queue_destroy(alloc_t* allocator, mpsc_queue_t* queue);
    void          mpsc_queue_enqueue(mpsc_queue_t* queue, void* item);
    bool          mpsc_queue_dequeue(mpsc_queue_t* queue, void* item);

    struct local_queue_t;
    local_queue_t* local_queue_create(alloc_t* allocator, s32 item_count, s32 item_size);
    void           local_queue_destroy(alloc_t* allocator, local_queue_t* queue);
    void           local_queue_enqueue(local_queue_t* queue, void* item);
    bool           local_queue_dequeue(local_queue_t* queue, void* item);

} // namespace ncore

#endif // __CJOBS_QUEUE_H__
