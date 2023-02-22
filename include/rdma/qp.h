#ifndef RDMA_QP_H_
#define RDMA_QP_H_
#include <string>

#include "rdma/mgr.h"
#include "rdma/predefs.h"

namespace rdma {
struct QP {
    QP(ibv_qp *qp, Context *ctx, int id);
    bool connect(const std::string &ctx_ip, int ctx_port, int qp_id);
    bool modifyToRTR(const QPInfo &remote_qp_info);
    bool modifyToRTS(bool rnr_retry = false);
    // UD send
    bool send(uint64_t source, uint64_t size, uint32_t lkey, ibv_ah *ah, uint32_t remote_qpn, uint64_t send_flags = 0);
    // RC / UC send
    bool send(uint64_t source, uint64_t size, uint32_t lkey, bool with_imm = false, int32_t imm = 0);
    bool recv(uint64_t source, uint64_t size, uint32_t lkey, uint64_t wr_id = 0, bool is_srq = false);
    void printState();

    void pollSendCQ(int num_entries, ibv_wc *wc);
    void pollRecvCQ(int num_entries, ibv_wc *wc);
    ibv_qp *qp;
    Context *ctx;
    int id;
};
}  // namespace rdma
#endif  // RDMA_QP_H_