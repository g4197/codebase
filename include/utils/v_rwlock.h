#ifndef V_RWLOCK_H_
#define V_RWLOCK_H_

#include <atomic>
#include <cstdint>
#include <thread>

extern uint32_t g_version;
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
    std::atomic<uint32_t> *payload() {
        return reinterpret_cast<std::atomic<uint32_t> *>(this);
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

#endif  // V_RWLOCK_H_