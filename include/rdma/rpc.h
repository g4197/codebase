#ifndef RDMA_Rpc_H_
#define RDMA_Rpc_H_

#include <unordered_map>

#include "rdma/context.h"
#include "rdma/qp.h"

template<>
class std::equal_to<ibv_gid> {
public:
    bool operator()(const ibv_gid &gid1, const ibv_gid &gid2) const {
        return memcmp(gid1.raw, gid2.raw, sizeof(gid1.raw)) == 0;
    }
};

namespace rdma {
// A simple RDMA RPC framework based on UD protocol, limited to MTU.
// An Rpc should be used by only a single thread.
// Server context and client context are treated differently.
// Async requests from client are supported.

constexpr int kMTU = 4096;
constexpr int kUDHeaderSize = sizeof(ibv_grh);

struct RpcContext;
struct ReqHandle;
struct RpcSession;
struct MsgBuf;
struct Rpc;

class GIDHash {
public:
    inline size_t operator()(const ibv_gid &gid) const {
        DLOG(INFO) << "GID raw is " << *(size_t *)gid.raw << " second is " << *(size_t *)(gid.raw + 8);
        return *(size_t *)(gid.raw + 8);
    }
};

struct RpcContext {
    RpcContext(const std::string &rpc_ip, int rpc_port, int numa = 0, uint8_t dev_port = 0,
               int gid_index = Context::kGIDAuto, int proto = Context::kInfiniBand, const char *ipv4_subnet = nullptr)
        : ctx(rpc_ip, rpc_port, dev_port, gid_index, proto, ipv4_subnet) {
        this->numa = numa;
    }

    inline void regFunc(uint8_t rpc_id, std::function<void(ReqHandle *, void *)> func) {
        is_server = true;
        funcs[rpc_id] = func;
    }

    MsgBuf *allocBuf();

    bool is_server;
    int numa;
    Context ctx;
    std::function<void(ReqHandle *, void *)> funcs[UINT8_MAX];
};

constexpr int kMsgBufAlign = 64;
struct alignas(kMsgBufAlign) MsgBuf {
private:
    MsgBuf(RpcContext *ctx) {
        mr = ctx->ctx.createMR(this->hdr, kUDHeaderSize + kMTU);
        lkey = mr->lkey;
    }

public:
    friend class RpcContext;
    uint8_t hdr[kUDHeaderSize];
    uint8_t buf[kMTU];
    uint32_t size;
    uint32_t lkey;
    ibv_mr *mr;
};

struct MsgBufPair {
    MsgBufPair(RpcContext *ctx) : finished(false) {
        send_buf = ctx->allocBuf();
        recv_buf = ctx->allocBuf();
    }

    MsgBuf *send_buf;
    MsgBuf *recv_buf;
    std::atomic<bool> finished;
};

// Single thread without coroutine.
struct Rpc {
    Rpc(RpcContext *rpc_ctx, void *context, int rpc_id);

    // Client API.
    RpcSession connect(const std::string &ctx_ip, int ctx_port, int rpc_id);
    void send(RpcSession *session, uint8_t rpc_id, MsgBufPair *buf);
    void runClientLoopOnce();
    bool tryRecv(MsgBufPair *msg);
    void recv(MsgBufPair *msg);

    // Server API.
    void runServerLoopOnce();
    static constexpr int kSrvBufCnt = Context::kQueueDepth;
    MsgBufPair *srv_bufs;
    ReqHandle *req_handles;
    std::unordered_map<ibv_gid, ibv_ah *, GIDHash> gid_ah_map;

    // Common.
    ibv_wc wc[Context::kQueueDepth];
    QP qp;
    int send_cnt;
    RpcContext *ctx;
    void *context;
};

struct RpcSession {
    void send(uint8_t rpc_id, MsgBufPair *buf);
    void recv(MsgBufPair *msg);
    Rpc *rpc;
    ibv_ah *ah;
    uint32_t qpn;
};

struct ReqHandle {
    Rpc *rpc;
    MsgBufPair *buf;
    ibv_ah *ah;
    uint32_t src_qp;
    void response();
};

}  // namespace rdma

#endif  // RDMA_Rpc_H_