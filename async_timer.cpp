
#include <unistd.h>
#include <sys/time.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <fcntl.h>

#include <thread>
#include <atomic>
#include <queue>

#include "async_timer.h"

class timer_reactor {
public:
    static timer_reactor& instance() {
        static timer_reactor ins;
        return ins;
    }

    void attach() {
        if (timer_cnt_++ == 0) {
            th_ = std::thread([&] {
                stopped_ = false;
                run();
            });
        }
    }

    void detach() {
        if (--timer_cnt_ == 0) {
            stopped_ = true;
            if (th_.joinable()) {
                th_.join();
            }
        }
    }

    void schedule_timer(uint32_t msecs) {

        struct timeval now;
        gettimeofday(&now, 0);

        uint64_t expires_at = now.tv_sec * 1000000 + now.tv_usec + msecs * 1000;
        bool was_empty = min_heap_.empty();
        uint64_t min_expire = 0;
        if (!was_empty) {
            min_expire = min_heap_.top();
        }

        min_heap_.push(expires_at);

        if (expires_at < min_expire || was_empty) {
            update_timeout();
        }
    }

private:
    timer_reactor()
    : timer_cnt_(0),
      stopped_(false),
      epoll_fd_(do_epoll_create()),
      timer_fd_(do_timer_create())
    {
        epoll_event evnt = { 0, { 0 } };
        evnt.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLPRI | EPOLLET;
        evnt.data.ptr = &timer_fd_;
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, timer_fd_, &evnt);
    }

    ~timer_reactor() {
        stopped_ = true;
    }

    void run()
    {
        enum { max_event = 128 };
        while (!stopped_)
        {
            bool check_timers = false;
            epoll_event events[max_event];
            int num_events = epoll_wait(epoll_fd_, events, max_event, -1);

            for (int i = 0; i < num_events; ++i) {
                void* ptr = events[i].data.ptr;
                if (ptr == &timer_fd_) {
                    puts("timeout");
                    check_timers = true;
                }
            }

            if (check_timers) {

                struct timeval now;
                gettimeofday(&now, 0);

                while (!min_heap_.empty())
                {
                    uint64_t now_msecs = now.tv_sec * 1000000 + now.tv_usec;
                    uint64_t tm = min_heap_.top();
                    if (now_msecs > tm) {
                        min_heap_.pop();
                    }
                }
            }
/*
            if (min_heap_.empty()) {
                stopped_ = true;
            }*/
        }
    }

    int do_epoll_create(){
        enum { epoll_size = 128 };
        int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
        if (epoll_fd == -1) {
            epoll_fd = epoll_create(epoll_size);
            if (epoll_fd != -1) {
                fcntl(epoll_fd, F_SETFD, FD_CLOEXEC);
            }
        }
        return epoll_fd;
    }

    int do_timer_create() {
        int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);

        if (timer_fd == -1) {
            timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);

            if (timer_fd != -1) {
                fcntl(timer_fd, F_SETFD, FD_CLOEXEC);
            }
        }
        return timer_fd;
    }

    int get_timeout(itimerspec& ts) {
        ts.it_interval.tv_sec = 0;
        ts.it_interval.tv_nsec = 0;

        long usec = 0;
        if (!min_heap_.empty()) {
            usec = min_heap_.top();

            struct timeval now;
            gettimeofday(&now, 0);
            usec -= now.tv_sec * 1000000 + now.tv_usec;
        }
        ts.it_value.tv_sec = usec / 1000000;
        ts.it_value.tv_nsec = usec ? (usec % 1000000) * 1000 : 1;

        return usec ? 0 : TFD_TIMER_ABSTIME;
    }

    void update_timeout() {
        itimerspec new_timeout;
        itimerspec old_timeout;
        int flags = get_timeout(new_timeout);
        timerfd_settime(timer_fd_, flags, &new_timeout, &old_timeout);
    }

    struct make_min {
        bool operator()(uint64_t const& l, uint64_t const& r) {
            return l > r;
        }
    };

private:
    std::thread th_;
    std::atomic_uint timer_cnt_;
    std::atomic_bool stopped_;
    int epoll_fd_;
    int timer_fd_;
    std::priority_queue<uint64_t, std::vector<uint64_t>, make_min> min_heap_;


};

class async_timer_impl
{
public:
    async_timer_impl(uint32_t msecs)
    : reactor_(&timer_reactor::instance())
    {
        reactor_->schedule_timer(msecs);
        reactor_->attach();
    }

    ~async_timer_impl() {
        reactor_->detach();
    }

    void set(uint32_t msecs) {
        reactor_->schedule_timer(msecs);
    }

    void reset() {
    }

private:
    timer_reactor* reactor_;
};

async_timer::async_timer(uint32_t msecs)
: impl_(new async_timer_impl(msecs))
{
}

async_timer::~async_timer()
{
    if (impl_ != nullptr) {
        delete impl_;
        impl_ = nullptr;
    }
}

void async_timer::set(uint32_t msecs)
{
    impl_->set(msecs);
}

void async_timer::reset()
{
    impl_->reset();
}

