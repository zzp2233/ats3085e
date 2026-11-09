/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#include "platform/gx_thread.h"
#include <atomic>
#include <condition_variable>
#include <mutex>

namespace gx {
namespace os {
struct Semaphore {
    std::atomic<int> count;
    std::condition_variable signal;
    std::mutex mutex;
};

static std::recursive_timed_mutex *mutex_cast(mutex_t mutex) {
    return reinterpret_cast<std::recursive_timed_mutex *>(mutex);
}

static Semaphore *semaphore_cast(semaphore_t semaphore) {
    return reinterpret_cast<Semaphore *>(semaphore);
}

mutex_t mutex_create(const char *) { return mutex_t(new std::recursive_timed_mutex); }

void mutex_delete(mutex_t mutex) { delete mutex_cast(mutex); }

bool mutex_take(mutex_t ptr, int time) {
    auto mutex = mutex_cast(ptr);
    if (time < 0) {
        mutex->lock();
        return true;
    }
    if (time == 0)
        return mutex->try_lock();
    return mutex->try_lock_for(std::chrono::milliseconds(time));
}

void mutex_release(mutex_t ptr) {
    auto mutex = mutex_cast(ptr);
    mutex->unlock();
}

semaphore_t semaphore_create(int value, const char *) {
    return semaphore_t(new Semaphore{{value}, {}, {}});
}

void semaphore_delete(semaphore_t semaphore) { delete semaphore_cast(semaphore); }

bool semaphore_wait(semaphore_t semaphore, int time) {
    auto *sem = semaphore_cast(semaphore);
    std::unique_lock<std::mutex> lock(sem->mutex);
    if (time < 0) {
        while (sem->count == 0)
            sem->signal.wait(lock);
        --sem->count;
        return true;
    }
    if (sem->signal.wait_for(lock, std::chrono::milliseconds(time),
                             [sem] { return sem->count > 0; })) {
        --sem->count;
        return true;
    }
    return false;
}

void semaphore_release(semaphore_t semaphore) {
    auto *sem = semaphore_cast(semaphore);
    std::unique_lock<std::mutex> lock(sem->mutex);
    ++sem->count;
    sem->signal.notify_all();
}
} // namespace os
} // namespace gx
