#ifndef UTILS_H_
#define UTILS_H_
#include <cxxabi.h>

#include <string>
#include <type_traits>
#include <typeinfo>

#include "utils/topology.h"

/*
 * Some necessary defs and some helper functions.
 */

template<typename T>
inline std::string type_name() {
    typedef typename std::remove_reference<T>::type TR;
    std::unique_ptr<char, void (*)(void *)> own(
#ifndef _MSC_VER
        abi::__cxa_demangle(typeid(TR).name(), nullptr, nullptr, nullptr),
#else
        nullptr,
#endif
        std::free);
    std::string r = own != nullptr ? own.get() : typeid(TR).name();
    if (std::is_const<TR>::value) r += " const";
    if (std::is_volatile<TR>::value) r += " volatile";
    if (std::is_lvalue_reference<T>::value) r += "&";
    else if (std::is_rvalue_reference<T>::value) r += "&&";
    return r;
}

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define forceinline inline __attribute__((always_inline))

constexpr int kCacheLineSize = 64;
const uint64_t kPMLineSize = 256;
constexpr uint64_t kMask48 = (1ul << 48) - 1;
constexpr uint64_t kThreadBufSize = 4096;
extern __thread char thread_buf[kThreadBufSize] __attribute__((aligned(kPMLineSize)));

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

#endif  // UTILS_H_