#include "cjobs/private/c_sys.h"

namespace cjobs
{
    namespace csys
    {
        void SysCPUCount(int32& numLogicalCores, int32& numPhysicalCores, int32& numPackages)
        {
            numLogicalCores  = 16;
            numPhysicalCores = 8;
            numPackages      = 1;
        }

    } // namespace csys
} // namespace cjobs