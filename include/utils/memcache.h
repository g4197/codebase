#ifndef MEMCACHE_H_
#define MEMCACHE_H_

// A simple memcached wrapper
#include <libmemcached/memcached.h>
#include <unistd.h>

#include <fstream>
#include <iostream>

#include "slice.h"
#include "str_utils.h"
#include "utils/log.h"

class Memcache {
public:
    Memcache() : memc_(nullptr) {}

    Memcache(const std::string &conf_path) {
        this->connect(conf_path);
    }

    ~Memcache() {
        this->disconnect();
    }

    inline bool connect(const std::string &ip, int port) {
        memcached_server_st *servers = NULL;
        memcached_return rc;

        memc_ = memcached_create(nullptr);
        servers = memcached_server_list_append(servers, ip.c_str(), port, &rc);
        rc = memcached_server_push(memc_, servers);

        if (rc != MEMCACHED_SUCCESS) {
            LOG(ERROR) << "Counld't add server:" << memcached_strerror(memc_, rc);
            return false;
        }

        memcached_behavior_set(memc_, MEMCACHED_BEHAVIOR_BINARY_PROTOCOL, 1);
        return true;
    }

    inline bool connect(const std::string &conf_path) {
        std::ifstream conf(conf_path);

        if (!conf) {
            LOG(ERROR) << "can't open memcached.conf";
            return false;
        }

        std::string addr, port;
        conf >> addr >> port;
        return connect(strip(addr), std::stoi(strip(port)));
    }

    inline bool disconnect() {
        if (memc_) {
            memcached_quit(memc_);
            memcached_free(memc_);
            memc_ = nullptr;
        }
        return true;
    }

    inline bool set(const Slice &key, const Slice &value) {
        memcached_return rc;
        rc = memcached_set(memc_, key.data(), key.size(), value.data(), value.size(), 0, 0);
        if (rc != MEMCACHED_SUCCESS) {
            LOG(ERROR) << "Couldn't store key:" << key.ToString() << ", value:" << value.ToString()
                       << ", error:" << memcached_strerror(memc_, rc);
            return false;
        }
        return true;
    }

    inline bool set(const Slice &key, const uint64_t &value) {
        memcached_return rc;
        std::string sv = std::to_string(value);
        rc = memcached_set(memc_, key.data(), key.size(), sv.c_str(), sv.size(), 0, 0);
        if (rc != MEMCACHED_SUCCESS) {
            LOG(ERROR) << "Couldn't store key:" << key.ToString() << ", value:" << value
                       << ", error:" << memcached_strerror(memc_, rc);
            return false;
        }
        return true;
    }

    inline bool get(const Slice &key, Slice &value) {
        // data in value should be freed.
        memcached_return rc;
        size_t vlen;
        char *ret = memcached_get(memc_, key.data(), key.size(), &vlen, nullptr, &rc);
        if (rc != MEMCACHED_SUCCESS) {
            LOG(ERROR) << "Couldn't get key:" << key.ToString() << ", error:" << memcached_strerror(memc_, rc);
            return false;
        }
        value = Slice(ret, vlen);
        return true;
    }

    inline bool get(const Slice &key, uint64_t &value) {
        Slice v;
        if (!get(key, v)) {
            return false;
        }
        value = std::stoull(v.ToString());
        free((char *)(v.data()));
        return true;
    }

    inline uint64_t faa(const Slice &key, uint64_t delta) {
        uint64_t res;
        while (true) {
            memcached_return rc =
                memcached_increment_with_initial(memc_, key.data(), key.size(), delta, delta, 0, &res);
            if (rc == MEMCACHED_SUCCESS) {
                return res;
            }
            usleep(10000);
        }
        /* unreachable */
    }

    inline void barrier(const Slice &name, uint64_t total) {
        faa(name, 1);
        uint64_t value = 0;
        while (true) {
            get(Slice(name), value);
            if (value % total == 0) {  // this can barrier multiple times.
                break;
            }
            usleep(1000);
        }
    }

private:
    memcached_st *memc_;
};

#endif  // MEMCACHE_H_