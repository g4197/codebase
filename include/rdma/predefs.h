#ifndef RDMA_PREDEFS_H_
#define RDMA_PREDEFS_H_

// #include <infiniband/mlx5dv.h>
#include <infiniband/verbs.h>

namespace rdma {
struct MR;
struct QP;
struct Context;

constexpr uint32_t kUDQkey = 0x11111111;
constexpr uint32_t kPSN = 3185;
constexpr uint32_t kDCTAccessKey = 3185;
constexpr int kMROnChipIdStart = 10000000;

#define rt_assert_ptr(input, msg) \
    do {                          \
        if (input == nullptr) {   \
            LOG(ERROR) << (msg);  \
            return nullptr;       \
        }                         \
    } while (0)

struct QPInfo {
    bool valid;
    uint16_t lid;
    uint32_t qpn;
    uint8_t gid[16];
};

struct alignas(32) MRInfo {
    bool valid;
    bool on_chip;
    uint32_t rkey;
    uint64_t addr;
    uint64_t size;
};

}  // namespace rdma

#endif  // RDMA_PREDEFS_H_