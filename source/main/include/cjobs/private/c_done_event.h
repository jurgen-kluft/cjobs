#ifndef __CJOBS_AUTO_RESET_EVENT_H__
#define __CJOBS_AUTO_RESET_EVENT_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    class alloc_t;

    struct event_done_t;

    void create_event_done(alloc_t* allocator, event_done_t*& event);
    void destroy_event_done(alloc_t* allocator, event_done_t* event);

    void wait_event_done(event_done_t* event);
    void signal_event_done(event_done_t* event, s32 count = 1);

} // namespace ncore

#endif // __CJOBS_AUTO_RESET_EVENT_H__