#include "cbase/c_allocator.h"
#include "cbase/c_context.h"
#include "ccore/c_target.h"
#include "cjobs/c_queue.h"

#include "cunittest/cunittest.h"

using namespace ncore;

UNITTEST_SUITE_BEGIN(spsc_queue)
{
    UNITTEST_FIXTURE(basic)
    {
        UNITTEST_ALLOCATOR;

        UNITTEST_FIXTURE_SETUP() {}
        UNITTEST_FIXTURE_TEARDOWN() {}

        UNITTEST_TEST(FillDrainWrap)
        {
            ncore::spsc_queue_t* queue = ncore::spsc_queue_create(Allocator, 4);
            CHECK_TRUE(queue != nullptr);

            CHECK_TRUE(ncore::queue_enqueue(queue, 1));
            CHECK_TRUE(ncore::queue_enqueue(queue, 2));
            CHECK_TRUE(ncore::queue_enqueue(queue, 3));
            CHECK_TRUE(ncore::queue_enqueue(queue, 4));
            CHECK_FALSE(ncore::queue_enqueue(queue, 5));

            ncore::u64 batch[2] = {};
            CHECK_EQUAL(2, ncore::queue_dequeue_multiple(queue, batch, 2));
            CHECK_EQUAL((ncore::u64)1, batch[0]);
            CHECK_EQUAL((ncore::u64)2, batch[1]);

            CHECK_TRUE(ncore::queue_enqueue(queue, 5));
            CHECK_TRUE(ncore::queue_enqueue(queue, 6));
            CHECK_FALSE(ncore::queue_enqueue(queue, 7));

            ncore::u64 value = 0;
            CHECK_TRUE(ncore::queue_dequeue(queue, value));
            CHECK_EQUAL((ncore::u64)3, value);
            CHECK_TRUE(ncore::queue_dequeue(queue, value));
            CHECK_EQUAL((ncore::u64)4, value);
            CHECK_TRUE(ncore::queue_dequeue(queue, value));
            CHECK_EQUAL((ncore::u64)5, value);
            CHECK_TRUE(ncore::queue_dequeue(queue, value));
            CHECK_EQUAL((ncore::u64)6, value);
            CHECK_FALSE(ncore::queue_dequeue(queue, value));

            ncore::queue_destroy(Allocator, queue);
        }
    }
}
UNITTEST_SUITE_END
