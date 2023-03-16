#ifndef THREAD_UTILS_H_
#define THREAD_UTILS_H_

#include <atomic>
#include <thread>

#include "defs.h"
#include "numa_utils.h"

// A wrapper for std::thread to make a "jthread" like c++20, with my_thread_id and my_numa_id initialized.

class Thread {
public:
    Thread() {}

    // Create a thread binding to NUMA numa, initializing my_thread_id and my_numa_id.
    template<class Function, class... Args>
    Thread(int numa, Function &&f, Args &&...args) {
        thread_id_ = tot_threads++;
        numa_id_ = numa;
        core_id_ = kBindCoreOffset[numa] + (numa_tot_threads[numa]++);
        thread_ = std::thread([&] {
            my_thread_id = thread_id_;
            my_numa_id = numa_id_;
            while (!finished_) {}
            f(args...);
        });
        bind_to_core(thread_.native_handle(), numa, core_id_);
        finished_ = true;
    }

    Thread &operator=(const Thread &rhs) = delete;

    inline Thread &operator=(Thread &&t) noexcept {
        thread_ = std::move(t.thread_);
        thread_id_ = t.thread_id_;
        numa_id_ = t.numa_id_;
        return *this;
    }

    ~Thread() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    inline void join() {
        thread_.join();
    }

    inline void detach() {
        thread_.detach();
    }

    inline std::thread::native_handle_type native_handle() {
        return thread_.native_handle();
    }

private:
    static std::atomic<int> tot_threads;
    static std::atomic<int> numa_tot_threads[kMaxNUMANodes];
    std::thread thread_;
    int numa_id_;
    int thread_id_;
    int core_id_;
    std::atomic<bool> finished_;
};

#endif  // THREAD_UTILS_H_