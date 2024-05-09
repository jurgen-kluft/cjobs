#ifndef __CJOBS_QUEUE_H__
#define __CJOBS_QUEUE_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    class alloc_t;

    namespace njob
    {
        struct queue_t;

        // Example:
        //    queue_t* queue = queue_create(allocator, 10, sizeof(int));
        //    int itemA = 0;
        //    int itemB = 1;
        //    queue_enqueue(queue, &itemA);
        //    queue_enqueue(queue, &itemB);
        //
        //    int item;
        //    queue_dequeue(queue, &item);
        //    queue_dequeue(queue, &item);
        //    queue_destroy(queue);

        queue_t* queue_create(alloc_t* allocator, s32 item_count, s32 item_size);
        void     queue_destroy(alloc_t* allocator, queue_t* queue);

        void queue_enqueue(queue_t* queue, void* item);
        bool queue_dequeue(queue_t* queue, void* item);

    } // namespace njob

} // namespace ncore

#endif // __CJOBS_QUEUE_H__
