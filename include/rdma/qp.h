#ifndef RDMA_QP_H_
#define RDMA_QP_H_
#include <string>

#include "rdma/predefs.h"

namespace rdma {
struct QPInfo {
    uint32_t qpn;
    uint16_t lid;
    uint8_t gid[16];
};

struct QP {
    QP(ibv_qp *qp, Context *ctx);
    bool publish(const std::string &key);
    bool getPublished(const std::string &key, QPInfo &info);
    bool modifyToRTR(const std::string &remote_key);
    bool modifyToRTR(const QPInfo &remote_qp_info);
    bool modifyToRTS(bool rnr_retry = false);
    // UD send
    bool send(uint64_t source, uint64_t size, uint32_t lkey, ibv_ah *ah, uint32_t remote_qpn, uint64_t send_flags = 0);
    // RC / UC send
    bool send(uint64_t source, uint64_t size, uint32_t lkey, bool with_imm = false, int32_t imm = 0);
    bool recv(uint64_t source, uint64_t size, uint32_t lkey, uint64_t wr_id = 0, bool is_srq = false);
    void printState();
    ibv_qp *qp;
    Context *ctx;
};
}  // namespace rdma
#endif  // RDMA_QP_H_