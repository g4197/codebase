#include <atomic>
#include <iostream>

#include "stdutils.h"

std::atomic<uint64_t> counter;
constexpr uint64_t kThreads = 64;

TotalOp total_op[kThreads];

int main() {
    Memcache memcache("../memcached.conf");
    char *k = "key1";
    Slice s;
    memcache.set(k, 0);
    memcache.get(k, s);
    std::cout << s.ToString() << std::endl;
    memcache.faa(k);
    memcache.get(k, s);
    std::cout << s.ToString() << std::endl;
}