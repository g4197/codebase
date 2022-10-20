#ifndef LOCK_TABLE_H_
#define LOCK_TABLE_H_

#include <pthread.h>

#include <functional>

#include "slice.h"

class LockTable {
public:
    LockTable(std::function<uint64_t(const Slice &)> hash_f, size_t size) : hash_f_(hash_f), size_(size) {
        locks_ = new pthread_rwlock_t[size];
        for (size_t i = 0; i < size; i++) {
            pthread_rwlock_init(&locks_[i], nullptr);
        }
    }
    ~LockTable();

    pthread_rwlock_t *rdlock(const Slice &key) {
        auto lock = &locks_[hash_f_(key) % size_];
        pthread_rwlock_rdlock(lock);
        return lock;
    }

    pthread_rwlock_t *wrlock(const Slice &key) {
        auto lock = &locks_[hash_f_(key) % size_];
        pthread_rwlock_wrlock(lock);
        return lock;
    }

    bool tryrdlock(const Slice &key) {
        return (pthread_rwlock_tryrdlock(&locks_[hash_f_(key) % size_]) == 0);
    }

    bool trywrlock(const Slice &key) {
        return (pthread_rwlock_trywrlock(&locks_[hash_f_(key) % size_]) == 0);
    }

    void unlock(pthread_rwlock_t *lock) {
        pthread_rwlock_unlock(lock);
    }

    void unlock(const Slice &key) {
        pthread_rwlock_unlock(&locks_[hash_f_(key) % size_]);
    }

private:
    std::function<uint64_t(const Slice &)> hash_f_;
    size_t size_;
    pthread_rwlock_t *locks_;
};

// RAII to avoid forgetting to unlock.
class LockGuard {
public:
    LockGuard(pthread_rwlock_t *lock) : lock_(lock) {}
    ~LockGuard() {
        if (lock_) {
            pthread_rwlock_unlock(lock_);
            lock_ = nullptr;
        }
    }

private:
    pthread_rwlock_t *lock_;
};

#endif  // LOCK_TABLE_H_