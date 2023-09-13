#ifndef HELPERS_H_
#define HELPERS_H_
#include <cxxabi.h>

#include <string>
#include <type_traits>
#include <typeinfo>

#include "utils/defs.h"
#include "utils/log.h"

/*
 * Some helper functions.
 */
inline void fence() {
    asm volatile("" ::: "memory");
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

inline bool cmpxchg16b(__int128_t *ptr, __int128_t old_value, __int128_t new_value) {
    return __sync_bool_compare_and_swap(ptr, old_value, new_value);
}

/*
inline void movdir64b(void *dst, void *src) {
    // Use the movdir64b to move 64B atomically from src to dst.
    // Load from src is not atomic, but store to dst is.
    asm volatile("movdir64b (%0), (%1)" : : "r"(src), "r"(dst) : "memory");
}
*/

#endif  // HELPERS_H_