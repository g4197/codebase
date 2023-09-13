#ifndef DEFS_H_
#define DEFS_H_
#include <cxxabi.h>

#include <string>
#include <type_traits>
#include <typeinfo>

#include "utils/log.h"

/*
 * Some necessary defs and some helper functions.
 */

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#ifndef forceinline
#define forceinline inline __attribute__((always_inline))
#endif

constexpr int kMaxNUMANodes = 8;  // Maybe need to modify this
constexpr int kBindCoreOffset[kMaxNUMANodes] = {
    0, 0, 0, 0, 0, 0, 0, 0
};  // Offset is i means Thread class's bind starts from i.

constexpr int hardware_destructive_interference_size = 128;
constexpr int kCacheLineSize = 64;
const uint64_t kPMLineSize = 256;
constexpr uint64_t kMask48 = (1ul << 48) - 1;
constexpr uint64_t kThreadBufSize = 4096;

extern __thread char thread_buf[kThreadBufSize] __attribute__((aligned(kPMLineSize)));

// Need to be initialized in advance
extern __thread int my_thread_id;
extern __thread int my_numa_id;
constexpr int kInvalidThreadNUMAId = -1;

#endif  // DEFS_H_