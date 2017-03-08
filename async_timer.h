#ifndef ASYNC_TIMER_H_
#define ASYNC_TIMER_H_

#include <stdint.h>

class async_timer_impl;
class async_timer
{
public:
    explicit async_timer(uint32_t msecs);

    ~async_timer();

    void set(uint32_t msecs);

    void reset();

private:
    async_timer_impl* impl_;
};

#endif // WATCHDOG_TIMER_H_
