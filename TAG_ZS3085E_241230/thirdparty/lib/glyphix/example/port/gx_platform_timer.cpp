/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <thread>

#include "platform/gx_os.h"
#include "platform/gx_thread.h"

namespace gx {
namespace os {

class Timer {
public:
    typedef std::function<void(void *)> Callback;

    Timer(double interval, Callback function, void *parameter);
    void start();
    void cancel();

public:
    struct Impl;

private:
    std::shared_ptr<Impl> pimpl;
};

using Clock = std::chrono::system_clock;
using TimePoint = Clock::time_point;
using Microseconds = std::chrono::microseconds;
using AlarmPtr = std::shared_ptr<Timer::Impl>;

class Timer::Impl {
public:
    Impl(double interval_, Callback function_, void *parameter_)
        : interval(interval_), function(function_), parameter(parameter_) {}

    void setup_alarm() {
        time = Clock::now() + Microseconds(static_cast<long int>(interval * 1000000));
    }

    double interval;          // unit second
    Timer::Callback function; // callback function
    void *parameter;
    TimePoint time; // expiration time
    std::atomic<bool> active{true};
};

class AlarmLooper {
public:
    AlarmLooper() = default;
    AlarmLooper(const AlarmLooper &) = delete;
    void operator=(const AlarmLooper &) = delete;

    void thread_safety_insert(AlarmPtr alarm) {
        std::unique_lock<std::mutex> lock(alarm_mutex);
        insert(alarm);
    }

    void insert(AlarmPtr alarm);
    void run();
    void stop();

private:
    std::list<AlarmPtr> alarm_list;
    TimePoint current_alarm;
    std::mutex alarm_mutex;
    std::condition_variable alarm_cond;
    std::atomic<bool> stopped{false};
};

/*
 * Insert alarm entry on list, in order.
 */
void AlarmLooper::insert(AlarmPtr alarm) {
    auto first = alarm_list.begin();
    auto last = alarm_list.end();

    /*
     * LOCKING PROTOCOL:
     *
     * This routine requires that the caller have locked the
     * alarm_mutex!
     */
    for (; first != last; ++first) {
        if ((*first) == alarm) { // already in alarm list
            return;
        }
        if ((*first)->time >= alarm->time) {
            alarm_list.insert(first, alarm);
            break;
        }
    }

    /*
     * If we reached the end of the list, insert the new alarm there.
     */
    if (first == last) {
        alarm_list.push_back(alarm);
    }

    /*
     * Wake the alarm thread if it is not busy (that is, if
     * current_alarm is 0, signifying that it's waiting for
     * work), or if the new alarm comes before the one on
     * which the alarm thread is waiting.
     */
    if (current_alarm == TimePoint{} || alarm->time < current_alarm) {
        current_alarm = alarm->time;
        alarm_cond.notify_one();
    }
}

/*
 * The alarm thread's start routine.
 */
void AlarmLooper::run() {
    AlarmPtr alarm;
    TimePoint now;
    bool expired;
    std::cv_status status;

    /*
     * Loop forever, processing commands. The alarm thread will
     * be disintegrated when the process exits. Lock the mutex
     * at the start -- it will be unlocked during condition
     * waits, so the main thread can insert alarms.
     */
    std::unique_lock<std::mutex> lock(alarm_mutex);
    while (true) {
        if (stopped)
            return;
        /*
         * If the alarm list is empty, wait until an alarm is
         * added. Setting current_alarm to 0 informs the insert
         * routine that the thread is not busy.
         */
        current_alarm = TimePoint{};
        while (alarm_list.empty()) {
            alarm_cond.wait(lock);
            if (stopped)
                return;
        }

        alarm = alarm_list.front();
        alarm_list.pop_front();
        now = Clock::now();
        expired = false;
        if (alarm->time > now) {
            current_alarm = alarm->time;
            while (current_alarm == alarm->time) {
                status = alarm_cond.wait_until(lock, alarm->time);
                if (status == std::cv_status::timeout) {
                    expired = true;
                    break;
                }
                if (stopped)
                    return;
            }
            if (!expired) {
                insert(alarm);
            }
        } else {
            expired = true;
        }

        if (expired) {
            if (alarm->active) {
                alarm->function(alarm->parameter);
            }
        }
    }
}

void AlarmLooper::stop() {
    stopped = true;
    alarm_cond.notify_one();
}

class TimerThread {
public:
    TimerThread();
    ~TimerThread();

    void insert_alarm(std::shared_ptr<Timer::Impl> timer);
    bool is_in_looper_thread() { return std::this_thread::get_id() == looper_thread.get_id(); }

    static TimerThread &get_instance() {
        static TimerThread timer_thread;
        return timer_thread;
    }

private:
    AlarmLooper alarm_looper;
    std::thread looper_thread;
};

TimerThread::TimerThread() { looper_thread = std::thread(&AlarmLooper::run, &alarm_looper); }

TimerThread::~TimerThread() {
    alarm_looper.stop();
    looper_thread.join();
}

void TimerThread::insert_alarm(std::shared_ptr<Timer::Impl> pimpl) {
    if (is_in_looper_thread()) {
        alarm_looper.insert(pimpl);
    } else {
        alarm_looper.thread_safety_insert(pimpl);
    }
}

Timer::Timer(double interval, Callback function, void *parameter) {
    pimpl = std::make_shared<Impl>(interval, function, parameter);
}

void Timer::start() {
    if (pimpl->active == false) { // already cancel
        return;
    }
    pimpl->setup_alarm();
    TimerThread::get_instance().insert_alarm(pimpl);
}

void Timer::cancel() { pimpl->active = false; }

static Timer *timer_cast(timer_t timer) { return reinterpret_cast<Timer *>(timer); }

timer_t timer_create(int time, const char *, void (*callback)(void *), void *parameter) {
    return timer_t(new Timer(time * 1.0, callback, parameter));
}

void timer_delete(timer_t timer) { delete timer_cast(timer); }

void timer_start(timer_t timer) {
    auto _timer = timer_cast(timer);
    _timer->start();
}

void timer_cancel(timer_t timer) {
    auto _timer = timer_cast(timer);
    _timer->cancel();
}
} // namespace os
} // namespace gx
