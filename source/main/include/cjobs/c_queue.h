#ifndef __CJOBS_QUEUE_H__
#define __CJOBS_QUEUE_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    class alloc_t;

    namespace njobs
    {
        struct queue_t;

        // Example:
        //    queue_t* queue = create_queue(allocator, 10, sizeof(int));
        //    int item = 0;
        //    queue_enqueue(queue, &item);
        //    queue_dequeue(queue, &item);
        //    destroy_queue(queue);

        queue_t* create_queue(alloc_t* allocator, s32 item_count, s32 item_size);
        void     destroy_queue(queue_t* queue);

        void queue_enqueue(queue_t* queue, void* item);
        bool queue_dequeue(queue_t* queue, void* item);

    } // namespace njobs

} // namespace ncore

#endif // __CJOBS_QUEUE_H__
