#ifndef RDMA_RPC_RC_RPC_H_
#define RDMA_RPC_RC_RPC_H_

#include <unordered_map>

#include "rdma/context.h"
#include "rdma/qp.h"
#include "rdma/rpc/common.h"
#include "rdma/rpc/shm.h"

namespace rdma {

struct QPCnt {
    QP *qp;
    int send_cnt;
    std::string oppo_ip;
    int oppo_port;
    int oppo_id;
};

// Single thread without coroutine.
// Out-of-order response is not supported.
struct Rpc {
    Rpc(RpcContext *rpc_ctx, void *context, int qp_id);

    // Client API.
    RpcSession connect(const std::string &ctx_ip, int ctx_port, int qp_id);
    void send(RpcSession *session, uint8_t rpc_id, MsgBufPair *buf);
    bool recv(MsgBufPair *msg, size_t retry_times = UINT64_MAX);

    // Server API.
    void runServerLoopOnce();
    void handleQPRequests();
    void handleSHMRequests();
    void postNextGroupRecv();
    static constexpr int kSrvBufCnt = 1024;

    MsgBufPair *srv_bufs;
    static constexpr int kRecvWrGroupCnt = 2;
    static constexpr int kRecvWrGroupSize = kSrvBufCnt / kRecvWrGroupCnt;
    ibv_sge *srv_recv_sges;
    ibv_recv_wr *srv_recv_wrs;
    ibv_recv_wr *srv_recv_wr_hdrs[kRecvWrGroupCnt];
    int cur_group;
    ibv_srq *srv_srq;
    ibv_cq *send_cq, *recv_cq;  // shared CQ for every srv QPs.
    int send_cnt;               // total send count.
    std::unordered_map<int, QPCnt> qpn_qp_map;

    template<class T, size_t sz>
    struct RingBuffer {
        T req_handles[sz + 1];
        T *ql{}, *qr{};
        RingBuffer() {
            ql = qr = &req_handles[0];
        }
        inline ReqHandle *pop() {
            if (unlikely(ql == qr)) {
                return nullptr;
            }
            T ret = *(ql++);
            if (ql > req_handles + sz) {
                ql = req_handles;
            }
            return ret;
        }

        inline void push(ReqHandle *handle) {
            *(qr++) = handle;
            if (qr > req_handles + sz) {
                qr = req_handles;
            }
        }
    };
    RingBuffer<ReqHandle *, Context::kQueueDepth * 4> req_handle_free_queue;

    // Common.
    ibv_wc wcs[Context::kQueueDepth];
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
    bool recv(MsgBufPair *msg, size_t retry_times = UINT64_MAX);
    void handleQPResponses();
    void handleSHMResponses();
    Rpc *rpc;
    QPCnt qp;
    int rmt_qp_id;

    // shm.
    ShmRpcRing *shm_ring;
    // don't batch too much...
    std::vector<std::pair<ShmRpcRingSlot *, MsgBufPair *>> tickets;
};

struct ReqHandle {
    Rpc *rpc;
    MsgBufPair *buf;
    QPCnt *qp;
    enum { kQP, kSHM } type;
    void response();
};

}  // namespace rdma

#endif  // RDMA_RPC_RC_RPC_H_