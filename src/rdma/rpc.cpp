#include "rdma/rpc.h"

#include "utils/defs.h"

namespace rdma {
RpcContext::RpcContext(const std::string &rpc_ip, int rpc_port, int id, int numa, uint8_t dev_port, int gid_index,
                       int proto, const char *ipv4_subnet)
    : ctx(rpc_ip, rpc_port, dev_port, gid_index, proto, ipv4_subnet) {
    this->my_ip = rpc_ip;
    this->my_port = rpc_port;
    this->id = id;
    this->numa = numa;
    this->is_server = false;

    this->funcs[kRpcNewConnection] = [](ReqHandle *handle, void *) {
        handle->buf->send_buf->size = 1;
        handle->response();
    };
}

MsgBuf *RpcContext::allocBuf() {
    MsgBuf *buf = new MsgBuf(this);
    return buf;
}

Rpc::Rpc(RpcContext *rpc_ctx, void *context, int qp_id) : ctx(rpc_ctx), context(context), conn_buf(rpc_ctx) {
    identifier.ctx_id = ctx->id;
    identifier.qp_id = qp_id;
    send_cnt = cur_group = 0;
    ibv_cq *send_cq = rpc_ctx->ctx.createCQ();
    ibv_cq *recv_cq = rpc_ctx->ctx.createCQ();
    qp = rpc_ctx->ctx.createQP(qp_id, IBV_QPT_UD, send_cq, recv_cq);
    qp.modifyToRTS(false);

    if (ctx->is_server) {
        DLOG(INFO) << "Is server, allocating buffers";
        if (posix_memalign((void **)&srv_bufs, kCacheLineSize, sizeof(MsgBuf) * kSrvBufCnt)) {
            LOG(FATAL) << "Failed to allocate memory for server buffers";
        }
        for (int i = 0; i < kSrvBufCnt; ++i) {
            new (srv_bufs + i) MsgBufPair(rpc_ctx);
        }

        srv_recv_sges = new ibv_sge[kSrvBufCnt];
        srv_recv_wrs = new ibv_recv_wr[kSrvBufCnt];
        for (int gi = 0; gi < kRecvWrGroupCnt; ++gi) {
            int start = gi * kRecvWrGroupSize;
            // Chained.
            int end = start + kRecvWrGroupSize;
            for (int i = start; i < end; ++i) {
                srv_recv_sges[i].addr = (uint64_t)srv_bufs[i].recv_buf->hdr;
                srv_recv_sges[i].length = kMTU;
                srv_recv_sges[i].lkey = srv_bufs[i].recv_buf->lkey;
                srv_recv_wrs[i].wr_id = (uint64_t)&srv_bufs[i];
                srv_recv_wrs[i].next = i == end - 1 ? nullptr : &srv_recv_wrs[i + 1];
                srv_recv_wrs[i].num_sge = 1;
                srv_recv_wrs[i].sg_list = &srv_recv_sges[i];
            }
            srv_recv_wr_hdrs[gi] = &srv_recv_wrs[start];
        }
        postNextGroupRecv();
        postNextGroupRecv();

        DLOG(INFO) << "Initialize shm buffer";
        shm_ring = ShmRpcRing::create(shm_key(ctx->my_ip, ctx->my_port, qp_id), kRingElemCnt);
        if (posix_memalign((void **)&shm_bufs, kCacheLineSize, sizeof(MsgBuf) * kRingElemCnt)) {
            LOG(FATAL) << "Failed to allocate memory for shm buffers";
        }

        for (int i = 0; i < kRingElemCnt; ++i) {
            new (shm_bufs + i) MsgBufPair(shm_ring->get(i), i);
        }
    }
}

RpcSession Rpc::connect(const std::string &ctx_ip, int ctx_port, int qp_id) {
    assert(!ctx->is_server);
    RpcSession session;
    memset(&session, 0, sizeof(RpcSession));
    if (ctx_ip == this->ctx->my_ip) {
        // Same machine, use shared memory.
        session.rpc = this;
        session.shm_ring = ShmRpcRing::open(shm_key(ctx_ip, ctx_port, qp_id), kRingElemCnt);
    } else {
        // Exchange connection info.
        std::string qp_info_str((char *)(&qp.info), sizeof(QPInfo));
        ctx->ctx.put(ctx_ip, ctx_port, identifier.key(), qp_info_str);

        QPInfo qp_info = ctx->ctx.getQPInfo(ctx_ip, ctx_port, qp_id);
        ibv_ah_attr ah_attr;
        ctx->ctx.fillAhAttr(&ah_attr, qp_info);
        session.rpc = this;
        session.ah = ibv_create_ah(ctx->ctx.pd, &ah_attr);
        session.qpn = qp_info.qpn;

        // Invalidate server-side cache.
        std::lock_guard<std::mutex> lock(conn_buf_mtx);
        this->send(&session, kRpcNewConnection, &conn_buf);
        this->recv(&conn_buf);
    }
    return session;
}

void Rpc::send(RpcSession *session, uint8_t rpc_id, MsgBufPair *buf) {
    assert(!ctx->is_server);
    buf->session = session;
    if (session->shm_ring != nullptr) {
        uint64_t ticket = session->shm_ring->clientSend(rpc_id, buf->send_buf);
        session->tickets.push_back(std::make_pair(session->shm_ring->get(ticket), buf));
    } else {
        auto &sbuf = buf->send_buf;
        auto &rbuf = buf->recv_buf;
        qp.recv((uint64_t)rbuf->hdr, kMTU, rbuf->lkey, (uint64_t)buf);

        // batch
        if (++send_cnt == Context::kQueueDepth) {
            qp.send((uint64_t)sbuf->buf, sbuf->size, sbuf->lkey, session->ah, session->qpn, IBV_SEND_SIGNALED, true,
                    RpcIdentifier(this->identifier, rpc_id).raw, (uint64_t)buf);
            qp.pollSendCQ(1, wcs);
            send_cnt = 0;
        } else {
            qp.send((uint64_t)sbuf->buf, sbuf->size, sbuf->lkey, session->ah, session->qpn, 0, true,
                    RpcIdentifier(this->identifier, rpc_id).raw, (uint64_t)buf);
        }
    }
}

void Rpc::handleQPResponses() {
    // poll recv CQ.
    assert(!ctx->is_server);
    int finished = ibv_poll_cq(qp.qp->recv_cq, Context::kQueueDepth, wcs);
    for (int i = 0; i < finished; ++i) {
        MsgBufPair *cur_pair = (MsgBufPair *)wcs[i].wr_id;
        cur_pair->recv_buf->size = wcs[i].byte_len - kUDHeaderSize;
        cur_pair->finished.store(true, std::memory_order_release);
    }
}

void Rpc::handleSHMResponses(RpcSession *session) {
    if (session->shm_ring != nullptr) {
        for (auto it = session->tickets.begin(); it != session->tickets.end();) {
            auto &p = *it;
            if (session->shm_ring->clientTryRecv(p.second->recv_buf, p.first)) {
                p.second->finished.store(true, std::memory_order_release);
                it = session->tickets.erase(it);
            } else {
                ++it;
            }
        }
    }
}

bool Rpc::tryRecv(MsgBufPair *msg) {
    assert(!ctx->is_server);
    handleQPResponses();
    handleSHMResponses(msg->session);
    return msg->finished;
}

void Rpc::recv(MsgBufPair *msg, size_t retry_times) {
    assert(!ctx->is_server);
    for (size_t i = 0; i < retry_times; ++i) {
        if (msg->finished) {
            msg->finished = false;
            return;
        }
        handleQPResponses();
        handleSHMResponses(msg->session);
    }
    LOG(INFO) << "Possible packet loss...";
}

void Rpc::runServerLoopOnce() {
    assert(ctx->is_server);
    handleQPRequests();
    handleSHMRequests();
}

void Rpc::handleQPRequests() {
    int finished = ibv_poll_cq(qp.qp->recv_cq, Context::kQueueDepth, wcs);
    for (int i = 0; i < finished; ++i) {
        MsgBufPair *cur_pair = (MsgBufPair *)wcs[i].wr_id;
        cur_pair->recv_buf->size = wcs[i].byte_len - kUDHeaderSize;
        RpcIdentifier identifier(wcs[i].imm_data);
        DLOG(INFO) << "Got identifier " << identifier.ctx_id << " " << (int)identifier.qp_id << " "
                   << (int)identifier.rpc_id;
        ibv_ah *ah = nullptr;
        uint32_t qpn = 0;
        if (unlikely(identifier.rpc_id == kRpcNewConnection)) {
            DLOG(INFO) << "New Connection";
            std::string info_str = ctx->ctx.mgr->get(identifier.key());
            QPInfo info;
            memcpy(&info, info_str.c_str(), sizeof(QPInfo));
            ibv_ah_attr attr;
            ctx->ctx.fillAhAttr(&attr, info);
            ah = ibv_create_ah(ctx->ctx.pd, &attr);
            qpn = info.qpn;
            if (!ah) {
                LOG(FATAL) << "Failed to create ah " << strerror(errno);
            }
            id_ah_map[identifier] = std::make_pair(ah, qpn);
        } else {
            ah = id_ah_map[identifier].first;
            qpn = id_ah_map[identifier].second;
            assert(ah != nullptr);
        }

        ReqHandle handle{ this, cur_pair, ah, qpn, ReqHandle::kQP };
        ctx->funcs[identifier.rpc_id](&handle, context);
    }
}

void Rpc::handleSHMRequests() {
    while (shm_ring->serverTryRecv(shm_ticket)) {
        ShmRpcRingSlot *slot = shm_ring->get(shm_ticket);
        ReqHandle handle{ this, &shm_bufs[shm_ticket % kRingElemCnt], nullptr, 0, ReqHandle::kSHM };
        ctx->funcs[slot->rpc_id](&handle, context);
        ++shm_ticket;
    }
}

void Rpc::postNextGroupRecv() {
    DLOG(INFO) << "postNextGroupRecv " << cur_group;
    ibv_recv_wr *bad_wr;
    ibv_post_recv(qp.qp, srv_recv_wr_hdrs[cur_group], &bad_wr);
    cur_group = (cur_group + 1) % kRecvWrGroupCnt;
}

void RpcSession::send(uint8_t rpc_id, MsgBufPair *buf) {
    rpc->send(this, rpc_id, buf);
}

void RpcSession::recv(MsgBufPair *msg, size_t retry_times) {
    rpc->recv(msg, retry_times);
}

void ReqHandle::response() {
    if (type == kQP) {
        auto &sbuf = buf->send_buf;
        // sync -> batch, 800K -> 4M (reduce poll cq race)
        if (++rpc->send_cnt >= Rpc::kRecvWrGroupSize) {
            rpc->qp.send((uint64_t)sbuf->buf, sbuf->size, sbuf->lkey, ah, src_qp, IBV_SEND_SIGNALED);
            rpc->postNextGroupRecv();
            ibv_wc wc;
            rpc->qp.pollSendCQ(1, &wc);
            rpc->send_cnt = 0;
        } else {
            rpc->qp.send((uint64_t)sbuf->buf, sbuf->size, sbuf->lkey, ah, src_qp, 0);
        }
    } else {
        assert(type == kSHM);
        rpc->shm_ring->serverSend(this->buf->ticket);
    }
}

}  // namespace rdma