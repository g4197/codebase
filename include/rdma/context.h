#ifndef RDMA_CONTEXT_H_
#define RDMA_CONTEXT_H_

#include <atomic>
#include <vector>

#include "rdma/mgr.h"
#include "rdma/predefs.h"

namespace rdma {

struct Context {
    // GIDs are currently used only for RoCE. This default value works for most
    // clusters, but we need a more robust GID selection method. Some observations:
    //  * On physical clusters, gid_index = 0 always works (in my experience)
    //  * On VM clusters (AWS/KVM), gid_index = 0 does not work, gid_index = 1 works
    //  * Mellanox's `show_gids` script lists all GIDs on all NICs
    Context(const std::string &rpc_ip, int rpc_port, uint8_t dev_port = 0, int gid_index = 0);
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

    QPInfo getQPInfo(const std::string &ctx_ip, int ctx_port, int qp_id);
    MRInfo getMRInfo(const std::string &ctx_ip, int ctx_port, int mr_id);

    uint8_t dev_index;
    uint8_t port;
    int gid_index;
    ibv_context *ctx;
    ibv_pd *pd;
    uint16_t lid;
    ibv_gid gid;
    int device_memory_size;

    std::atomic<int> qp_id;
    std::atomic<int> mr_id;
    std::atomic<int> mr_on_chip_id;

    ManagerServer mgr;

    SimpleKV<std::string, ManagerClient> mgr_clients;
    std::mutex mgr_clients_mutex;

    void printDeviceInfoEx();

private:
    void checkDMSupported();
    ibv_mr *createMROnChip(void *addr, uint64_t size);
};
}  // namespace rdma

#endif  // RDMA_CONTEXT_H_