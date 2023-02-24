#ifndef RDMA_CONTEXT_H_
#define RDMA_CONTEXT_H_

#include <atomic>
#include <vector>

#include "rdma/mgr.h"
#include "rdma/predefs.h"

namespace rdma {

struct Context {
    // RoCE v1: gid = 2
    // RoCE v2: gid = 3
    // normal: gid = 0 or 1
    enum { kGIDAuto = -1 };
    enum { kInfiniBand, kRoCEv2 };
    // If proto == kRoCEv2, ipv4_subnet is required
    Context(const std::string &rpc_ip, int rpc_port, uint8_t dev_port = 0, int gid_index = kGIDAuto,
            int proto = kInfiniBand, const char *ipv4_subnet = nullptr);
    ~Context();

    int identifyGID(ibv_context *ctx, uint8_t port, int proto, const char *ipv4_subnet = nullptr);

    static constexpr int kQueueDepth = 128;
    ibv_mr *createMR(int id, void *addr, uint64_t size, bool odp, bool mw_binding);
    ibv_mr *createMR(void *addr, uint64_t size, bool odp = false, bool mw_binding = false);
    ibv_mr *createMROnChip(void *addr, uint64_t size);
    ibv_mr *createMROnChip(int id, void *addr, uint64_t size);
    ibv_cq *createCQ(int cqe = kQueueDepth, void *cq_ctx = nullptr, ibv_comp_channel *channel = nullptr);
    ibv_srq *createSRQ(int queue_depth = kQueueDepth, int sgl_size = 1);
    QP createQP(int id, ibv_qp_type mode, ibv_cq *send_cq, ibv_cq *recv_cq, ibv_srq *srq = nullptr,
                int queue_depth = kQueueDepth, int sgl_size = 1, uint32_t max_inline_data = 0);
    QP createQP(ibv_qp_type mode, ibv_cq *send_cq, ibv_cq *recv_cq, ibv_srq *srq = nullptr,
                int queue_depth = kQueueDepth, int sgl_size = 1, uint32_t max_inline_data = 0);
    QP createQP(ibv_qp_type mode, ibv_cq *cq, ibv_srq *srq = nullptr, int queue_depth = kQueueDepth, int sgl_size = 1,
                uint32_t max_inline_data = 0);
    void fillAhAttr(ibv_ah_attr *attr, uint32_t remote_lid, const uint8_t *remote_gid);
    void fillAhAttr(ibv_ah_attr *attr, const QPInfo &qp_info);

    QPInfo getQPInfo(const std::string &ctx_ip, int ctx_port, int qp_id);
    MRInfo getMRInfo(const std::string &ctx_ip, int ctx_port, int mr_id);
    void put(const std::string &ctx_ip, int ctx_port, const std::string &key, const std::string &value);

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

    ManagerServer *mgr;

    SimpleKV<std::string, ManagerClient *> mgr_clients;
    std::mutex mgr_clients_mutex;

    void printDeviceInfoEx();

private:
    ManagerClient *connect(const std::string &ctx_ip, int ctx_port);
    void checkDMSupported();
};
}  // namespace rdma

#endif  // RDMA_CONTEXT_H_