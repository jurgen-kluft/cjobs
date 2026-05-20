#include "cbase/c_allocator.h"
#include "cbase/c_context.h"
#include "ccore/c_target.h"
#include "cjobs/c_queue.h"

#include "cunittest/cunittest.h"

using namespace ncore;

UNITTEST_SUITE_BEGIN(spmc_queue)
{
    UNITTEST_FIXTURE(basic)
    {
        UNITTEST_ALLOCATOR;

        UNITTEST_FIXTURE_SETUP() {}
        UNITTEST_FIXTURE_TEARDOWN() {}

        UNITTEST_TEST(FillDrainWrap)
        {
            ncore::spmc_queue_t* queue = ncore::spmc_queue_create(Allocator, 4);
            CHECK_TRUE(queue != nullptr);

            CHECK_TRUE(ncore::queue_enqueue(queue, 20));
            CHECK_TRUE(ncore::queue_enqueue(queue, 21));
            CHECK_TRUE(ncore::queue_enqueue(queue, 22));
            CHECK_TRUE(ncore::queue_enqueue(queue, 23));
            CHECK_FALSE(ncore::queue_enqueue(queue, 24));

            ncore::u64 value = 0;
            CHECK_TRUE(ncore::queue_dequeue(queue, value));
            CHECK_EQUAL((ncore::u64)20, value);
            CHECK_TRUE(ncore::queue_dequeue(queue, value));
            CHECK_EQUAL((ncore::u64)21, value);

            CHECK_TRUE(ncore::queue_enqueue(queue, 24));
            CHECK_TRUE(ncore::queue_enqueue(queue, 25));
            CHECK_FALSE(ncore::queue_enqueue(queue, 26));

            CHECK_TRUE(ncore::queue_dequeue(queue, value));
            CHECK_EQUAL((ncore::u64)22, value);
            CHECK_TRUE(ncore::queue_dequeue(queue, value));
            CHECK_EQUAL((ncore::u64)23, value);
            CHECK_TRUE(ncore::queue_dequeue(queue, value));
            CHECK_EQUAL((ncore::u64)24, value);
            CHECK_TRUE(ncore::queue_dequeue(queue, value));
            CHECK_EQUAL((ncore::u64)25, value);
            CHECK_FALSE(ncore::queue_dequeue(queue, value));

            ncore::queue_destroy(Allocator, queue);
        }
    }
}
UNITTEST_SUITE_END
