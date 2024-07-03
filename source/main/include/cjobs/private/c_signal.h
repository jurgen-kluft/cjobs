#ifndef __CJOBS_AUTO_RESET_EVENT_H__
#define __CJOBS_AUTO_RESET_EVENT_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    class alloc_t;

    // A signal is a synchronization primitive that allows one thread to signal one or
    // more other threads that an event has occurred.
    struct signal_t;

    void signal_create(alloc_t* allocator, signal_t*& event);
    void signal_destroy(alloc_t* allocator, signal_t* event);
    bool signal_set(signal_t* event);                  // Returns true if the event was signaled, false if it was already signaled
    void signal_reset(signal_t* event);
    void signal_wait(signal_t* event, bool autoReset); // This can only be used from one thread

} // namespace ncore

#endif // __CJOBS_AUTO_RESET_EVENT_H__
