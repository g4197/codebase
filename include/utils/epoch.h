#ifndef EPOCH_H_
#define EPOCH_H_

#include <assert.h>

#include <atomic>

#include "defs.h"

// A simple epoch-based synchronization algorithm
// Depending on "my_thread_id" variable to identify local epoch.
// Bind core first!!!!

static constexpr uint64_t kOutEpochVersion = UINT64_MAX;
using std::atomic;
class Epoch {
public:
    Epoch() : version_(kOutEpochVersion) {}

    inline void in(const atomic<uint64_t> &version) {
        version_.store(version.load(std::memory_order_acquire), std::memory_order_release);
    }
    inline void out() {
        version_.store(kOutEpochVersion, std::memory_order_release);
    }

    inline uint64_t version() {
        return version_.load(std::memory_order_acquire);
    }

private:
    // to avoid false sharing.
    char pad_front[kCacheLineSize];
    alignas(hardware_destructive_interference_size) atomic<uint64_t> version_;
    char pad_back[kCacheLineSize - sizeof(atomic<uint64_t>)];
};

class EpochManager {
public:
    EpochManager(int64_t num_threads) : num_threads_(num_threads) {
        epochs_ = new Epoch[num_threads];
    }

    ~EpochManager() {
        delete epochs_;
    }

    inline void in(uint64_t thread_id) {
        epochs_[thread_id].in(g_version_);
    }

    inline void out(uint64_t thread_id) {
        epochs_[thread_id].out();
    }

    inline int64_t g_version() {
        return g_version_.load(std::memory_order_acquire);
    }

    inline uint64_t version(uint64_t thread_id) {
        return epochs_[thread_id].version();
    }

    inline uint64_t incAndWait(uint64_t delta = 1) {
        // Stunned in this memory order...
        // But in x86, any memory order will be compiled to a fence, so it's OK.
        uint64_t ret = g_version_.fetch_add(delta, std::memory_order_seq_cst);
        bool should_wait = true;
        while (should_wait) {
            should_wait = false;
            for (int i = 0; i < num_threads_; ++i) {
                uint64_t v = epochs_[i].version();
                if (v != kOutEpochVersion && v < ret) {
                    should_wait = true;
                    // CPU pause
                    asm volatile("pause" ::: "memory");
                    break;
                }
            }
        }
        return ret;
    }

private:
    int64_t num_threads_;
    Epoch *epochs_;
    std::atomic<uint64_t> g_version_;
};

// RAII
class EpochGuard {
public:
    EpochGuard(EpochManager &manager) : manager_(manager) {
        assert(my_thread_id != kInvalidThreadNUMAId);
        manager_.in(my_thread_id);
    }

    ~EpochGuard() {
        manager_.out(my_thread_id);
    }

    uint64_t version() {
        return manager_.version(my_thread_id);
    }

private:
    EpochManager &manager_;
};

#endif  // EPOCH_H_