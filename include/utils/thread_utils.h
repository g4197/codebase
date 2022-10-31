#ifndef THREAD_UTILS_H_
#define THREAD_UTILS_H_

#include <atomic>
#include <thread>

#include "defs.h"
#include "numa_utils.h"

// A wrapper for std::thread, with my_thread_id and my_numa_id initialized.

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
            f(args...);
        });
        bind_to_core(thread_.native_handle(), numa, core_id_);
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
};

#endif  // THREAD_UTILS_H_