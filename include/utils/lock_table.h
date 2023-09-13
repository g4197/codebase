#ifndef LOCK_TABLE_H_
#define LOCK_TABLE_H_

#include <pthread.h>

#include <functional>
#include <shared_mutex>
#include <unordered_map>

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

    inline pthread_rwlock_t *rdlock(const Slice &key) {
        auto lock = &locks_[hash_f_(key) % size_];
        pthread_rwlock_rdlock(lock);
        return lock;
    }

    inline uint64_t pos(const Slice &key) {
        return hash_f_(key) % size_;
    }

    inline pthread_rwlock_t *wrlock(const Slice &key) {
        auto lock = &locks_[hash_f_(key) % size_];
        pthread_rwlock_wrlock(lock);
        return lock;
    }

    inline bool tryrdlock(const Slice &key) {
        return (pthread_rwlock_tryrdlock(&locks_[hash_f_(key) % size_]) == 0);
    }

    inline bool trywrlock(const Slice &key) {
        return (pthread_rwlock_trywrlock(&locks_[hash_f_(key) % size_]) == 0);
    }

    inline void unlock(pthread_rwlock_t *lock) {
        pthread_rwlock_unlock(lock);
    }

    inline void unlock(const Slice &key) {
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
    LockGuard &operator=(const LockGuard &) = delete;
    LockGuard &operator=(LockGuard &&rhs) {
        lock_ = rhs.lock_;
        rhs.lock_ = nullptr;
        return *this;
    }
    ~LockGuard() {
        if (lock_) {
            pthread_rwlock_unlock(lock_);
            lock_ = nullptr;
        }
    }

private:
    pthread_rwlock_t *lock_;
};

// ~53Mops/s for uint64_t Key with 16 threads.
template<class K, class HashFunction = std::hash<K>>
class LockHashTable {
public:
    LockHashTable(int n_shards = 512) : n_shards_(n_shards) {
        locks_ = new HashTable[n_shards];
    }

    size_t pos(const K &key) {
        return HashFunction()(key);
    }

    void rdlock(const K &key) {
        size_t h = pos(key);
        rdlock(key, h);
    }

    void rdlock(const K &key, size_t h) {
        run(key, h, [](std::pair<int, pthread_rwlock_t> &it) {
            it.first++;
            pthread_rwlock_rdlock(&it.second);
            return 0;
        });
    }

    int tryrdlock(const K &key) {
        return tryrdlock(key, pos(key));
    }

    int tryrdlock(const K &key, size_t h) {
        return run(key, h, [](std::pair<int, pthread_rwlock_t> &it) {
            it.first++;
            return pthread_rwlock_tryrdlock(&it.second);
        });
    }

    void wrlock(const K &key) {
        size_t h = pos(key);
        wrlock(key, h);
    }

    void wrlock(const K &key, size_t h) {
        run(key, h, [](std::pair<int, pthread_rwlock_t> &it) {
            it.first++;
            pthread_rwlock_wrlock(&it.second);
            return 0;
        });
    }

    inline void unlock(const K &key) {
        size_t h = pos(key);
        unlock(key, h);
    }

    inline void unlock(const K &key, size_t h) {
        run(key, h, [](std::pair<int, pthread_rwlock_t> &it) {
            it.first--;
            pthread_rwlock_unlock(&it.second);
            return 0;
        });
    }

private:
    int run(const K &key, size_t h, std::function<int(std::pair<int, pthread_rwlock_t> &)> f) {
        auto &table = locks_[h % n_shards_];
        std::lock_guard<std::shared_mutex> guard(table.mutex);
        if (table.locks.find(key) == table.locks.end()) {
            pthread_rwlock_t m{};
            table.locks[key] = std::make_pair(0, m);
        }
        auto it = table.locks.find(key);
        int ret = f(it->second);
        if (it->second.first == 0) {
            table.locks.erase(it);
        }
        return ret;
    }
    int n_shards_;
    struct HashTable {
        std::unordered_map<K, std::pair<int, pthread_rwlock_t>, HashFunction> locks{};
        std::shared_mutex mutex{};
    } *locks_;
};

#endif  // LOCK_TABLE_H_