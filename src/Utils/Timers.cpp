
#include "Timers.hpp"

uint64_t GetTimeNow()
{
   return sys_time_get_system_time() / 1000;
}

uint64_t GetCurrentTick()
{
    uint64_t freq = sys_time_get_timebase_frequency();
    double dFreq = ((double)freq) / 1000.0;

    uint64_t newTime;
    SYS_TIMEBASE_GET(newTime);

    return (uint64_t)((double(newTime)) / dFreq);
}

void Sleep(uint64_t ms)
{
   sys_timer_usleep(ms * 1000);
}