#ifndef __TIMER_H__
#define __TIMER_H__

namespace cjobs
{
    typedef unsigned long long uint64;
    typedef uint64             ticks_t;

    class Timer
    {
    public:
        static void    Init();
        static ticks_t Current();
        static ticks_t Lap(ticks_t t);
        static double  ElapsedMs(ticks_t t);
        static double  ElapsedUs(ticks_t t);

    private:
        static uint64 mFrequency;
    };

} // namespace cjobs
#endif /* !__TIMER_H__ */