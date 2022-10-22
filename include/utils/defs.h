#ifndef UTILS_H_
#define UTILS_H_
#include <cxxabi.h>

#include <string>
#include <type_traits>
#include <typeinfo>

#include "glog/logging.h"

/*
 * Some necessary defs and some helper functions.
 */

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define forceinline inline __attribute__((always_inline))

constexpr int kMaxNUMANodes = 8; // Maybe need to modify this
constexpr int hardware_destructive_interference_size = 128;
constexpr int kCacheLineSize = 64;
const uint64_t kPMLineSize = 256;
constexpr uint64_t kMask48 = (1ul << 48) - 1;
constexpr uint64_t kThreadBufSize = 4096;

extern __thread char thread_buf[kThreadBufSize] __attribute__((aligned(kPMLineSize)));

// Need to be initialized in advance
extern __thread int my_thread_id;
extern __thread int my_numa_id;

inline void fence() {
    asm volatile("" ::: "memory");
}

inline void sfence() {
    asm volatile("sfence" ::: "memory");
}

inline uint64_t rdtsc() {
    uint64_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

inline void prefetch(const void *ptr) {
    typedef struct {
        char x[kCacheLineSize];
    } cacheline_t;
    asm volatile("prefetcht0 %0" : : "m"(*(const cacheline_t *)ptr));
}

inline void rt_assert(bool condition, std::string throw_str) {
    if (unlikely(!condition)) {
        LOG(ERROR) << "Assertion failed: " << throw_str;
    }
}

#endif  // UTILS_H_