#ifndef __CJOBS_AUTO_RESET_EVENT_H__
#define __CJOBS_AUTO_RESET_EVENT_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    class alloc_t;

    struct autoreset_event_t;

    void create(alloc_t* allocator, autoreset_event_t*& event, s32 initialCount = 0);
    void destroy(alloc_t* allocator, autoreset_event_t* event);

    void wait(autoreset_event_t* event);
    void signal(autoreset_event_t* event, s32 count = 1);

} // namespace ncore

#endif // __CJOBS_AUTO_RESET_EVENT_H__