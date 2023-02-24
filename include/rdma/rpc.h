#ifndef RDMA_Rpc_H_
#define RDMA_Rpc_H_

#include <unordered_map>

#include "rdma/context.h"
#include "rdma/qp.h"

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

union RpcIdentifier {
    uint32_t raw;
    struct {
        uint16_t ctx_id;
        uint8_t qp_id;
        uint8_t rpc_id;
    } __attribute__((packed));

    RpcIdentifier() {}

    RpcIdentifier(uint32_t raw) {
        this->raw = raw;
    }

    RpcIdentifier(const RpcIdentifier &rhs, uint8_t rpc_id) {
        this->raw = rhs.raw;
        this->rpc_id = rpc_id;
    }

    RpcIdentifier(const std::string &key) {
        this->raw = stoi(key.substr(3));
        this->rpc_id = 0;
    }

    bool operator==(const RpcIdentifier &other) const {
        return this->ctx_id == other.ctx_id && this->rpc_id == other.rpc_id;
    }

    std::string key() {
        RpcIdentifier other(*this, 0);
        return "id_" + std::to_string(other.raw);
    }
};

class RpcIdentifierHash {
public:
    size_t operator()(const RpcIdentifier &id) const {
        return id.ctx_id << 8 | id.qp_id;
    }
};

struct RpcContext {
    // id is globally unique.
    RpcContext(const std::string &rpc_ip, int rpc_port, int id, int numa = 0, uint8_t dev_port = 0,
               int gid_index = Context::kGIDAuto, int proto = Context::kInfiniBand, const char *ipv4_subnet = nullptr)
        : ctx(rpc_ip, rpc_port, dev_port, gid_index, proto, ipv4_subnet) {
        this->id = id;
        this->numa = numa;
    }

    inline void regFunc(uint8_t rpc_id, std::function<void(ReqHandle *, void *)> func) {
        is_server = true;
        funcs[rpc_id] = func;
    }

    MsgBuf *allocBuf();

    bool is_server;
    int id;
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
    Rpc(RpcContext *rpc_ctx, void *context, int qp_id);

    // Client API.
    RpcSession connect(const std::string &ctx_ip, int ctx_port, int qp_id);
    void send(RpcSession *session, uint8_t rpc_id, MsgBufPair *buf);
    void runClientLoopOnce();
    bool tryRecv(MsgBufPair *msg);
    void recv(MsgBufPair *msg);

    // Server API.
    void runServerLoopOnce();
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
    std::unordered_map<RpcIdentifier, ibv_ah *, RpcIdentifierHash> id_ah_map;

    // Common.
    RpcIdentifier identifier;
    ibv_wc wcs[Context::kQueueDepth];
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