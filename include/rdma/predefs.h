#ifndef RDMA_EXTERN_H_
#define RDMA_EXTERN_H_

#include <infiniband/mlx5dv.h>
#include <infiniband/verbs.h>

namespace rdma {
struct QP;
struct Context;

constexpr uint32_t kUDQkey = 0x11111111;
constexpr uint32_t kPSN = 3185;
constexpr uint32_t kDCTAccessKey = 3185;

#define rt_assert_ptr(input, msg) \
    do {                          \
        if (input == nullptr) {   \
            LOG(ERROR) << msg;    \
            return nullptr;       \
        }                         \
    } while (0)
}  // namespace rdma

#endif  // RDMA_EXTERN_H_