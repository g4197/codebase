#ifndef RDMA_SIMPLE_KV_H_
#define RDMA_SIMPLE_KV_H_

#include <string>
#include <unordered_map>

#include "utils/log.h"

namespace rdma {

// A simple concurrent KVStore, not for performance.

template<class K, class V>
class SimpleKV {
public:
    SimpleKV() {
        pthread_rwlock_init(&kv_mutex_, nullptr);
    }

    inline void put(const K &key, const V &value) {
        pthread_rwlock_wrlock(&kv_mutex_);
        kv_[key] = value;
        pthread_rwlock_unlock(&kv_mutex_);
    }

    inline void insert(const K &key, const V &value) {
        pthread_rwlock_wrlock(&kv_mutex_);
        if (kv_.find(key) == kv_.end()) {
            kv_[key] = value;
        }
        pthread_rwlock_unlock(&kv_mutex_);
    }

    inline V &get(const K &key) {
        pthread_rwlock_rdlock(&kv_mutex_);
        if (kv_.find(key) != kv_.end()) {
            pthread_rwlock_unlock(&kv_mutex_);
            return kv_[key];
        }
        return null_ref_;
    }

    inline bool exists(const K &key) {
        bool ret;
        pthread_rwlock_rdlock(&kv_mutex_);
        ret = (kv_.find(key) != kv_.end());
        pthread_rwlock_unlock(&kv_mutex_);
        return ret;
    }

private:
    std::unordered_map<K, V> kv_;
    pthread_rwlock_t kv_mutex_;
    V null_ref_;
};
}  // namespace rdma

#endif  // RDMA_SIMPLE_KV_H_