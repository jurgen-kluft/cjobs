#include "cjobs/c_jobs.h"
#include "cjobs/private/c_sys.h"

#include "cunittest/cunittest.h"

UNITTEST_SUITE_BEGIN(TestSys)
{
    UNITTEST_FIXTURE(main)
    {
        UNITTEST_FIXTURE_SETUP() {}
        UNITTEST_FIXTURE_TEARDOWN() {}

        UNITTEST_TEST(TestList)
        { 
            cjobs::csys::int32 physicalCores;
            cjobs::csys::int32 logicalCores;
            cjobs::csys::int32 packages;
            cjobs::csys::SysCPUCount(logicalCores, physicalCores, packages);
        }
    }
}
UNITTEST_SUITE_END
