#ifndef RDMA_QP_H_
#define RDMA_QP_H_
#include <string>

#include "rdma/mgr.h"
#include "rdma/predefs.h"
#include "utils/defs.h"

namespace rdma {
struct alignas(kCacheLineSize) QP {
    QP();
    QP(ibv_qp *qp, Context *ctx, int id);
    bool connect(const std::string &ctx_ip, int ctx_port, int qp_id);
    bool modifyToRTR(const QPInfo &remote_qp_info);
    bool modifyToRTS(bool rnr_retry = false);
    // UD send
    bool send(uint64_t source, uint64_t size, uint32_t lkey, ibv_ah *ah, uint32_t remote_qpn, uint64_t send_flags = 0,
              bool with_imm = false, int32_t imm = 0, uint64_t wr_id = 0);
    // RC / UC send
    bool send(uint64_t source, uint64_t size, uint32_t lkey, bool with_imm = false, int32_t imm = 0);
    bool recv(uint64_t source, uint64_t size, uint32_t lkey, uint64_t wr_id = 0, bool is_srq = false);
    bool read(uint64_t source, uint64_t dest, uint64_t size, uint32_t lkey, uint32_t rkey, uint64_t send_flags = 0,
              uint64_t wr_id = 0);
    bool write(uint64_t source, uint64_t dest, uint64_t size, uint32_t lkey, uint32_t rkey, uint64_t send_flags = 0,
               uint64_t wr_id = 0);
    bool faa(uint64_t source, uint64_t dest, uint64_t delta, uint32_t lkey, uint32_t rkey);
    bool cas(uint64_t source, uint64_t dest, uint64_t compare, uint64_t swap, uint32_t lkey, uint32_t rkey,
             uint64_t send_flags = 0, uint64_t wr_id = 0);

    void printState();

    void pollSendCQ(int num_entries, ibv_wc *wc);
    void pollRecvCQ(int num_entries, ibv_wc *wc);
    ibv_qp *qp;
    Context *ctx;
    int id;
    QPInfo info;

    ibv_sge sge;
    ibv_send_wr send_wr;
    ibv_recv_wr recv_wr;
    ibv_send_wr *bad_send_wr;
    ibv_recv_wr *bad_recv_wr;
};

}  // namespace rdma
#endif  // RDMA_QP_H_