#ifndef RDMA_CONTEXT_H_
#define RDMA_CONTEXT_H_

#include <vector>

#include "rdma/predefs.h"
#include "utils/memcache.h"

namespace rdma {

struct Context {
    Context(int dev_index = 0, uint8_t port = 1, int gid_index = 0, const std::string &conf_path = "../memcached.conf");
    ~Context();

    static constexpr int kQueueDepth = 128;
    ibv_mr *createMR(void *addr, uint64_t size, bool on_chip = false, bool odp = false, bool mw_binding = false);
    ibv_cq *createCQ(int cqe = kQueueDepth, void *cq_ctx = nullptr, ibv_comp_channel *channel = nullptr);
    ibv_srq *createSRQ(int queue_depth = kQueueDepth, int sgl_size = 1);
    QP createQP(ibv_qp_type mode, ibv_cq *send_cq, ibv_cq *recv_cq, ibv_srq *srq = nullptr,
                int queue_depth = kQueueDepth, int sgl_size = 1, uint32_t max_inline_data = 0);
    QP createQP(ibv_qp_type mode, ibv_cq *cq, ibv_srq *srq = nullptr, int queue_depth = kQueueDepth, int sgl_size = 1,
                uint32_t max_inline_data = 0);
    void fillAhAttr(ibv_ah_attr *attr, uint32_t remote_lid, const uint8_t *remote_gid);

    uint8_t dev_index;
    uint8_t port;
    int gid_index;
    ibv_context *ctx;
    ibv_pd *pd;
    uint16_t lid;
    ibv_gid gid;
    int device_memory_size;
    Memcache *memcache;

    void printDeviceInfoEx();

private:
    void checkDMSupported();
    ibv_mr *createMROnChip(void *addr, uint64_t size);
};
}  // namespace rdma

#endif  // RDMA_CONTEXT_H_