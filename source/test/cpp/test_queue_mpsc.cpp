#include "cbase/c_allocator.h"
#include "cbase/c_context.h"
#include "ccore/c_target.h"
#include "cjobs/c_queue.h"

#include "cunittest/cunittest.h"

using namespace ncore;

UNITTEST_SUITE_BEGIN(mpsc_queue)
{
    UNITTEST_FIXTURE(basic)
    {
        UNITTEST_ALLOCATOR;

        UNITTEST_FIXTURE_SETUP() {}
        UNITTEST_FIXTURE_TEARDOWN() {}

        UNITTEST_TEST(DrainAndRelease)
        {
            ncore::mpsc_queue_t* queue = ncore::mpsc_queue_create(Allocator, 2, 4);
            CHECK_TRUE(queue != nullptr);

            CHECK_TRUE(ncore::queue_enqueue(queue, 0, 100));
            CHECK_TRUE(ncore::queue_enqueue(queue, 1, 200));
            CHECK_TRUE(ncore::queue_enqueue(queue, 0, 101));
            CHECK_TRUE(ncore::queue_enqueue(queue, 1, 201));

            ncore::u32 begin = 0;
            ncore::u32 end   = 0;
            CHECK_TRUE(ncore::queue_inspect(queue, begin, end));

            bool seen100 = false;
            bool seen101 = false;
            bool seen200 = false;
            bool seen201 = false;

            ncore::u32 idx = begin;
            while (idx != end)
            {
                ncore::u64 value = 0;
                CHECK_TRUE(ncore::queue_dequeue(queue, idx, end, value));
                switch (value)
                {
                    case 100: seen100 = true; break;
                    case 101: seen101 = true; break;
                    case 200: seen200 = true; break;
                    case 201: seen201 = true; break;
                    default: CHECK_TRUE(false); break;
                }
            }

            CHECK_TRUE(seen100 );
            CHECK_TRUE(seen101 );
            CHECK_TRUE(seen200 );
            CHECK_TRUE(seen201 );

            CHECK_EQUAL((ncore::s8)0, ncore::queue_release(queue, idx, end));
            CHECK_FALSE(ncore::queue_inspect(queue, begin, end));

            ncore::queue_destroy(Allocator, queue);
        }
    }
}
UNITTEST_SUITE_END
