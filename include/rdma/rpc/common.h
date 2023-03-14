#ifndef RDMA_RPC_COMMON_H_
#define RDMA_RPC_COMMON_H_

#include <unordered_map>

#include "rdma/context.h"
#include "rdma/qp.h"
#include "utils/defs.h"

namespace rdma {
// A simple RDMA RPC framework based on UD protocol, limited to MTU.
// An Rpc should be used by only a single thread.
// Server context and client context are treated differently.
// Async requests from client are supported.

constexpr uint8_t kRpcNewConnection = UINT8_MAX;
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
        return this->ctx_id == other.ctx_id && this->qp_id == other.qp_id;
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
               int gid_index = Context::kGIDAuto, int proto = Context::kInfiniBand, const char *ipv4_subnet = nullptr);

    inline void regFunc(uint8_t rpc_id, std::function<void(ReqHandle *, void *)> func) {
        if (rpc_id == kRpcNewConnection) {
            LOG(ERROR) << "rpc_id should not be kRpcNewConnection";
            return;
        }
        is_server = true;
        funcs[rpc_id] = func;
    }

    MsgBuf *allocBuf();

    std::string my_ip;
    int my_port;
    bool is_server;
    int id;
    int numa;
    Context ctx;
    std::function<void(ReqHandle *, void *)> funcs[UINT8_MAX + 1];
};

constexpr int kMsgBufAlign = 64;
struct alignas(kMsgBufAlign) MsgBuf {
    MsgBuf() {
        mr = nullptr;
    }

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

struct alignas(kCacheLineSize) ShmRpcRingSlot {
    MsgBuf send_buf;
    MsgBuf recv_buf;
    uint64_t turn_;
    uint8_t rpc_id;
    bool finished_;
    std::atomic<uint64_t> *turn() {
        return reinterpret_cast<std::atomic<uint64_t> *>(&turn_);
    }
    std::atomic<bool> *finished() {
        return reinterpret_cast<std::atomic<bool> *>(&finished_);
    }
};

struct MsgBufPair {
    MsgBufPair(RpcContext *ctx) : finished(false) {
        send_buf = ctx->allocBuf();
        recv_buf = ctx->allocBuf();
    }

    MsgBufPair(ShmRpcRingSlot *slot, uint64_t ticket) {
        send_buf = &slot->send_buf;
        recv_buf = &slot->recv_buf;
        this->ticket = ticket;
    }

    MsgBuf *send_buf;
    MsgBuf *recv_buf;
    RpcSession *session;  // Used by shm communication
    uint64_t ticket;      // Used by shm communication
    std::atomic<bool> finished;
};

}  // namespace rdma

#endif  // RDMA_RPC_COMMON_H_