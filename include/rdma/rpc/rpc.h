#ifndef RDMA_RPC_RPC_H_
#define RDMA_RPC_RPC_H_

#include <unordered_map>

#include "rdma/context.h"
#include "rdma/qp.h"
#include "rdma/rpc/common.h"
#include "rdma/rpc/shm.h"

namespace rdma {

// Single thread without coroutine.
struct Rpc {
    Rpc(RpcContext *rpc_ctx, void *context, int qp_id);

    // Client API.
    RpcSession connect(const std::string &ctx_ip, int ctx_port, int qp_id);
    void send(RpcSession *session, uint8_t rpc_id, MsgBufPair *buf);
    void handleQPResponses();
    void handleSHMResponses(RpcSession *session);
    bool tryRecv(MsgBufPair *msg);
    void recv(MsgBufPair *msg, size_t retry_times = UINT64_MAX);

    // Server API.
    void runServerLoopOnce();
    void handleQPRequests();
    void handleSHMRequests();
    void postNextGroupRecv();
    static constexpr int kSrvBufCnt = Context::kQueueDepth;

    MsgBufPair *srv_bufs;
    static constexpr int kRecvWrGroupCnt = 2;
    static constexpr int kRecvWrGroupSize = kSrvBufCnt / kRecvWrGroupCnt;
    ibv_sge *srv_recv_sges;
    ibv_recv_wr *srv_recv_wrs;
    ibv_recv_wr *srv_recv_wr_hdrs[kRecvWrGroupCnt];
    int cur_group;

    ReqHandle *req_handles;

    // Note: RpcIdentifier's id should be 0.
    std::unordered_map<RpcIdentifier, std::pair<ibv_ah *, uint32_t>, RpcIdentifierHash> id_ah_map;

    // Common.
    RpcIdentifier identifier;
    ibv_wc wcs[Context::kQueueDepth];
    QP qp;
    int send_cnt;
    RpcContext *ctx;
    void *context;
    MsgBufPair conn_buf;
    std::mutex conn_buf_mtx;

    // shm.
    ShmRpcRing *shm_ring;
    uint64_t shm_ticket;
    MsgBufPair *shm_bufs;
    inline std::string shm_key(const std::string &ip, int port, int qp_id) {
        return "shm-rpc" + ip + ":" + std::to_string(port) + ":" + std::to_string(qp_id);
    }
    static constexpr int kRingElemCnt = 8192;
};

struct RpcSession {
    void send(uint8_t rpc_id, MsgBufPair *buf);
    void recv(MsgBufPair *msg, size_t retry_times = UINT64_MAX);
    Rpc *rpc;
    ibv_ah *ah;
    uint32_t qpn;

    // shm.
    ShmRpcRing *shm_ring;
    // don't batch too much...
    std::vector<std::pair<ShmRpcRingSlot *, MsgBufPair *>> tickets;
};

struct ReqHandle {
    Rpc *rpc;
    MsgBufPair *buf;
    ibv_ah *ah;
    uint32_t src_qp;
    enum { kQP, kSHM } type;
    void response();
};

}  // namespace rdma

#endif  // RDMA_RPC_RPC_H_