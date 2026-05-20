#include "cbase/c_allocator.h"
#include "cbase/c_context.h"
#include "ccore/c_target.h"
#include "cjobs/c_queue.h"

#include "cunittest/cunittest.h"

using namespace ncore;

UNITTEST_SUITE_BEGIN(local_queue)
{
    UNITTEST_FIXTURE(basic)
    {
        UNITTEST_ALLOCATOR;

        UNITTEST_FIXTURE_SETUP() {}
        UNITTEST_FIXTURE_TEARDOWN() {}

        UNITTEST_TEST(FillDrainWrap)
        {
            ncore::local_queue_t* queue = ncore::local_queue_create(Allocator, 4);
            CHECK_TRUE(queue != nullptr);

            CHECK_TRUE(ncore::queue_enqueue(queue, 30));
            CHECK_TRUE(ncore::queue_enqueue(queue, 31));
            CHECK_TRUE(ncore::queue_enqueue(queue, 32));
            CHECK_TRUE(ncore::queue_enqueue(queue, 33));
            CHECK_FALSE(ncore::queue_enqueue(queue, 34));

            ncore::u64 value = 0;
            CHECK_TRUE(ncore::queue_dequeue(queue, value));
            CHECK_EQUAL((ncore::u64)30, value);
            CHECK_TRUE(ncore::queue_dequeue(queue, value));
            CHECK_EQUAL((ncore::u64)31, value);

            CHECK_TRUE(ncore::queue_enqueue(queue, 34));
            CHECK_TRUE(ncore::queue_enqueue(queue, 35));
            CHECK_FALSE(ncore::queue_enqueue(queue, 36));

            CHECK_TRUE(ncore::queue_dequeue(queue, value));
            CHECK_EQUAL((ncore::u64)32, value);
            CHECK_TRUE(ncore::queue_dequeue(queue, value));
            CHECK_EQUAL((ncore::u64)33, value);
            CHECK_TRUE(ncore::queue_dequeue(queue, value));
            CHECK_EQUAL((ncore::u64)34, value);
            CHECK_TRUE(ncore::queue_dequeue(queue, value));
            CHECK_EQUAL((ncore::u64)35, value);
            CHECK_FALSE(ncore::queue_dequeue(queue, value));

            ncore::queue_destroy(Allocator, queue);

        }
    }
}
UNITTEST_SUITE_END
