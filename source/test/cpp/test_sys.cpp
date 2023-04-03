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

            virtual void v_Deallocate(void* ptr) { gTestAllocator->deallocate(ptr); }
        };

        void JobRun(void* params) {}

        UNITTEST_TEST(TestList)
        {
            Allocator          allocator;
            cjobs::JobsManager manager = cjobs::JobsManager::Create(&allocator);

            manager.RegisterJob(JobRun, "TestJob");

            cjobs::JobsThreadDescr threads[] = {
                //                       name, core, stack-size
                cjobs::JobsThreadDescr("JobThread1", 0, 0),
                cjobs::JobsThreadDescr("JobThread2", 0, 0),
                cjobs::JobsThreadDescr("JobThread3", 0, 0),
                cjobs::JobsThreadDescr("JobThread4", 0, 0),
            };

            manager.Init(threads, _countof(threads));

            cjobs::JobsListDescr myListDescr("List1", cjobs::JOBSLIST_PRIORITY_LOW, 256, 256, cjobs::COLOR_RED);
            cjobs::JobsList      myList = manager.AllocJobList(myListDescr);

            myList.AddJob(JobRun, (void*)"test");
            myList.Submit();
            myList.Wait();

            manager.FreeJobList(myList);
            manager.Shutdown();

            cjobs::JobsManager::Destroy(manager);
        }
    }
}
UNITTEST_SUITE_END
