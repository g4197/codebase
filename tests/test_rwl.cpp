#include <bits/stdc++.h>

#include "stdutils.h"

using namespace std;

uint32_t g_version = 1;
struct VersionedRWLock {
    union {
        struct {
            uint32_t version : 20;
            uint32_t reader : 10;
            uint32_t writer : 1;
            uint32_t fix : 1;
        };
        uint32_t raw;
    };
    atomic<uint32_t> *payload() {
        return reinterpret_cast<atomic<uint32_t> *>(this);
    }

    VersionedRWLock() : raw(0) {}

    VersionedRWLock(uint32_t raw) : raw(raw) {}

    void lock_shared() {
        while (true) {
            VersionedRWLock snapshot(payload()->load(std::memory_order_relaxed));
            VersionedRWLock next = snapshot;
            if (snapshot.writer == 1) {
                continue;
            }
            if (snapshot.reader == 0) {
                next.version = g_version;
            }
            next.reader++;
            if (payload()->compare_exchange_strong(snapshot.raw, next.raw, std::memory_order_acquire)) {
                break;
            }
            std::this_thread::yield();
        }
    }

    void unlock_shared() {
        while (true) {
            VersionedRWLock snapshot = VersionedRWLock(payload()->load(std::memory_order_relaxed));
            VersionedRWLock next = snapshot;
            if (--next.reader == 0) {
                next.version = 0;
            }
            if (payload()->compare_exchange_strong(snapshot.raw, next.raw, std::memory_order_release)) {
                break;
            }
        }
    }

    void lock() {
        // acquire write lock.
        while (true) {
            VersionedRWLock snapshot = VersionedRWLock(payload()->load(std::memory_order_relaxed));
            VersionedRWLock next = snapshot;
            if (snapshot.writer == 1) continue;
            next.writer = 1;
            if (payload()->compare_exchange_strong(snapshot.raw, next.raw, std::memory_order_acquire)) {
                break;
            }
        }
        // wait for readers unlock.
        while (true) {
            VersionedRWLock snapshot = VersionedRWLock(payload()->load(std::memory_order_relaxed));
            if (snapshot.reader == 0) break;
        }
    }

    void unlock() {
        payload()->store(0, std::memory_order_release);
    }
};

constexpr int kMaxThreads = 28;
TotalOp total_op[kMaxThreads];
RWSpinLock rwlock;
struct alignas(16) CMTime {
    uint64_t ctime;
    uint64_t mtime;
} cmtime;

int main() {
    Benchmark::run(Benchmark::kNUMA0, kMaxThreads, total_op, [&]() {
        while (true) {
            int x;
            // update cmtime.
            rwlock.lock_shared();
            uint64_t tm = time(nullptr);
            do {
                CMTime old = cmtime;
                CMTime cur{ tm, tm };
                // use cmpxchg16b to guarantee atomicity of modification of ctime and mtime.
                // ctime increases monotomically.
                if (old.ctime >= tm ||
                    __sync_bool_compare_and_swap((__int128_t *)&cmtime, *(__int128_t *)&old, *(__int128_t *)&cur)) {
                    break;
                }
            } while (true);
            rwlock.unlock_shared();

            // spin for 2us.
            auto now = std::chrono::high_resolution_clock::now();
            while (true) {
                auto cnow = std::chrono::high_resolution_clock::now();
                if (cnow - now > std::chrono::microseconds(2)) break;
            }
            total_op[my_thread_id].ops++;
        }
    }).printTputAndJoin();
}