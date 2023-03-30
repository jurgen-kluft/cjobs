#include "cbase/c_base.h"
#include "cbase/c_allocator.h"

#include "cjobs/c_jobs.h"
#include "cjobs/private/c_sys.h"

#include "cunittest/cunittest.h"

extern ncore::alloc_t* gTestAllocator;

UNITTEST_SUITE_BEGIN(TestSys)
{
    UNITTEST_FIXTURE(main)
    {
        UNITTEST_FIXTURE_SETUP() {}
        UNITTEST_FIXTURE_TEARDOWN() {}

        class Allocator : public cjobs::Alloc
        {
        public:
            virtual void* v_Allocate(cjobs::uint64 size, cjobs::int32 alignment, cjobs::int32 tag = 0) 
            { 
                void* mem = gTestAllocator->allocate((ncore::s32)size, (ncore::s32)alignment);
                return mem;
            }

            virtual void  v_Deallocate(void* ptr) { gTestAllocator->deallocate(ptr);            }
        };

        UNITTEST_TEST(TestList)
        {
            Allocator           allocator;
            cjobs::JobsManager* manager = cjobs::CreateJobManager(&allocator);

            cjobs::csys::core_t threads[] = {
                0, 0
            };

            manager->Init(threads, _countof(threads));
        }


    }
}
UNITTEST_SUITE_END
