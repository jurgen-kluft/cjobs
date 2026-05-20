#include "cbase/c_allocator.h"
#include "cbase/c_context.h"
#include "ccore/c_target.h"
#include "cjobs/c_queue.h"

#include "cunittest/cunittest.h"

using namespace ncore;

UNITTEST_SUITE_BEGIN(mpmc_queue)
{
    UNITTEST_FIXTURE(basic)
    {
        UNITTEST_ALLOCATOR;

        UNITTEST_FIXTURE_SETUP() {}
        UNITTEST_FIXTURE_TEARDOWN() {}

        UNITTEST_TEST(FillDrainWrap)
        {
            ncore::mpmc_queue_t* queue = ncore::mpmc_queue_create(Allocator, 4);
            CHECK_TRUE(queue != nullptr);

            CHECK_TRUE(ncore::queue_enqueue(queue, 10));
            CHECK_TRUE(ncore::queue_enqueue(queue, 11));
            CHECK_TRUE(ncore::queue_enqueue(queue, 12));
            CHECK_TRUE(ncore::queue_enqueue(queue, 13));
            CHECK_FALSE(ncore::queue_enqueue(queue, 14));

            ncore::u64 value = 0;
            CHECK_TRUE(ncore::queue_dequeue(queue, value));
            CHECK_EQUAL((ncore::u64)10, value);
            CHECK_TRUE(ncore::queue_dequeue(queue, value));
            CHECK_EQUAL((ncore::u64)11, value);

            CHECK_TRUE(ncore::queue_enqueue(queue, 14));
            CHECK_TRUE(ncore::queue_enqueue(queue, 15));
            CHECK_FALSE(ncore::queue_enqueue(queue, 16));

            CHECK_TRUE(ncore::queue_dequeue(queue, value));
            CHECK_EQUAL((ncore::u64)12, value);
            CHECK_TRUE(ncore::queue_dequeue(queue, value));
            CHECK_EQUAL((ncore::u64)13, value);
            CHECK_TRUE(ncore::queue_dequeue(queue, value));
            CHECK_EQUAL((ncore::u64)14, value);
            CHECK_TRUE(ncore::queue_dequeue(queue, value));
            CHECK_EQUAL((ncore::u64)15, value);
            CHECK_FALSE(ncore::queue_dequeue(queue, value));

            ncore::queue_destroy(Allocator, queue);
        }
    }
}
UNITTEST_SUITE_END
