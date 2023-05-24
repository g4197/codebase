#ifndef V_RWLOCK_H_
#define V_RWLOCK_H_

#include <atomic>
#include <cstdint>
#include <thread>

extern uint32_t g_version;
struct VersionedRWLock {
    enum { kLockFail, kLockSuccess, kLockFix };
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
            next.reader++;
            next.version = g_version;
            if (payload()->compare_exchange_strong(snapshot.raw, next.raw, std::memory_order_acquire)) {
                break;
            }
        }
    }

    bool lock_fix() {
        while (true) {
            VersionedRWLock snapshot(payload()->load(std::memory_order_relaxed));
            // already locked.
            if (snapshot.version == g_version && snapshot.fix == 1) return false;
            VersionedRWLock next = snapshot;
            next.version = g_version;
            next.fix = 1;
            if (payload()->compare_exchange_strong(snapshot.raw, next.raw, std::memory_order_acquire)) {
                break;
            }
        }
        return true;
    }

    void unlock_fix_lock_shared() {
        VersionedRWLock snapshot(payload()->load(std::memory_order_relaxed));
        snapshot.fix = 0;
        // No other readers in the fix.
        snapshot.reader = 1;
        snapshot.version = g_version;
        payload()->store(snapshot.raw, std::memory_order_release);
    }

    int try_lock_shared() {
        while (true) {
            VersionedRWLock snapshot(payload()->load(std::memory_order_relaxed));
            VersionedRWLock next = snapshot;
            // This is before next condition to handle fix-then-crash.
            if (unlikely(snapshot.version != g_version && snapshot.version != 0)) {
                return lock_fix() ? kLockFix : kLockFail;
            }
            if (unlikely(snapshot.fix == 1)) return kLockFail;
            if (snapshot.writer == 1) return kLockFail;
            next.reader++;
            next.version = g_version;
            if (payload()->compare_exchange_strong(snapshot.raw, next.raw, std::memory_order_acquire)) {
                break;
            }
        }
        return kLockSuccess;
    }

    void unlock_shared() {
        while (true) {
            VersionedRWLock snapshot(payload()->load(std::memory_order_relaxed));
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
            VersionedRWLock snapshot(payload()->load(std::memory_order_relaxed));
            VersionedRWLock next = snapshot;
            // TODO: Lock fix for set_permission to fix mtime
            if (unlikely(snapshot.fix == 1)) continue;
            if (snapshot.writer == 1) continue;
            next.writer = 1;
            if (payload()->compare_exchange_strong(snapshot.raw, next.raw, std::memory_order_acquire)) {
                break;
            }
        }
        // wait for readers unlock.
        while (true) {
            VersionedRWLock snapshot(payload()->load(std::memory_order_relaxed));
            if (snapshot.reader == 0) break;
        }
    }

    void unlock() {
        VersionedRWLock snapshot(payload()->load(std::memory_order_relaxed));
        VersionedRWLock next = 0;
        payload()->store(next.raw, std::memory_order_release);
    }
};

#endif  // V_RWLOCK_H_