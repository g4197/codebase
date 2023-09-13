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

    for (int i = 0; i < UINT8_MAX; ++i) {
        this->funcs[i] = [](ReqHandle *, void *) { LOG(INFO) << "RPC not implemented"; };
    }

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

    if (posix_memalign((void **)&srv_bufs, kCacheLineSize, sizeof(MsgBufPair) * kSrvBufCnt)) {
        LOG(FATAL) << "Failed to allocate memory for server buffers";
    }
    for (int i = 0; i < kSrvBufCnt; ++i) {
        new (srv_bufs + i) MsgBufPair(rpc_ctx, true);
    }

    // All the recv bufs are managed by server.
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

    if (posix_memalign((void **)&srv_shm_bufs, kCacheLineSize, sizeof(MsgBuf *) * kSrvBufCnt)) {
        LOG(FATAL) << "Failed to allocate memory for server shm buffers";
    }

    for (int i = 0; i < kSrvBufCnt; ++i) {
        srv_shm_bufs[i] = rpc_ctx->allocBuf();
    }
    srv_shm_buf_idx = 0;

    if (ctx->is_server) {
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
    RpcSession session;
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
        this->recv(&conn_buf, 1000000000);
    }
    return session;
}
void Rpc::send(RpcSession *session, uint8_t rpc_id, MsgBufPair *buf) {
    buf->session = session;
    if (session->shm_ring != nullptr) {
        buf->recv_buf = this->srv_shm_bufs[this->srv_shm_buf_idx];
        this->srv_shm_buf_idx = (this->srv_shm_buf_idx + 1) % kSrvBufCnt;
        uint64_t ticket = session->shm_ring->clientSend(rpc_id, buf->send_buf);
        session->tickets.push_back(std::make_pair(session->shm_ring->get(ticket), buf));
    } else {
        auto &sbuf = buf->send_buf;
        // fill header and seq.
        sbuf->rpc_hdr = session->rpcHeader(rpc_id);

        // maintain maps.
        this->seq_buf_map.insert(std::make_pair(sbuf->rpc_hdr.seq, buf));
        DLOG(INFO) << "Send with identifier " << sbuf->rpc_hdr.identifier.ctx_id << " "
                   << sbuf->rpc_hdr.identifier.qp_id << " " << (int)sbuf->rpc_hdr.identifier.rpc_id << " seq "
                   << sbuf->rpc_hdr.seq << " " << my_thread_id;

        if (++send_cnt >= Rpc::kRecvWrGroupSize) {
            qp.send((uint64_t)&sbuf->rpc_hdr, sbuf->size + sizeof(RpcHeader), sbuf->lkey, session->ah, session->qpn,
                    IBV_SEND_SIGNALED);
            ibv_wc wc;
            qp.pollSendCQ(1, &wc);
            send_cnt = 0;
        } else {
            qp.send((uint64_t)&sbuf->rpc_hdr, sbuf->size + sizeof(RpcHeader), sbuf->lkey, session->ah, session->qpn);
        }
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
    handleQP();
    if (shm_ring != nullptr) handleSHMRequests();
    handleSHMResponses(msg->session);
    if (msg->finished) {
        msg->finished = false;
        return true;
    }
    return false;
}

void Rpc::recv(MsgBufPair *msg, size_t retry_times) {
    DLOG(INFO) << "Receiving for pair " << msg << " " << my_thread_id;
    for (size_t i = 0; i < retry_times; ++i) {
        if (msg->finished) {
            DLOG(INFO) << "Finished for pair " << msg << " " << my_thread_id;
            msg->finished = false;
            return;
        }
        handleQP();
        if (shm_ring != nullptr) handleSHMRequests();
        handleSHMResponses(msg->session);
    }
    LOG(INFO) << "Possible packet loss for rpc id " << (int)msg->send_buf->rpc_hdr.identifier.rpc_id << " sequence "
              << msg->send_buf->rpc_hdr.seq << "... " << my_thread_id << " for msg " << msg << " " << this->ctx->my_ip
              << ":" << this->ctx->my_port << " id " << this->qp.id;
}

void Rpc::runServerLoopOnce() {
    assert(ctx->is_server);
    handleQP();
    handleSHMRequests();
}

// may recursively call.
void Rpc::handleQP() {
    ibv_wc wcs[Context::kQueueDepth];
    int finished = ibv_poll_cq(qp.qp->recv_cq, Context::kQueueDepth, wcs);
    recv_cnt += finished;
    if (unlikely(recv_cnt >= Context::kQueueDepth)) {
        LOG(INFO) << "Recv Queue is exhausted...";
    }
    while (recv_cnt >= kRecvWrGroupSize) {
        postNextGroupRecv();
        recv_cnt -= kRecvWrGroupSize;
    }
    for (int i = 0; i < finished; ++i) {
        MsgBufPair *cur_pair = (MsgBufPair *)wcs[i].wr_id;
        cur_pair->recv_buf->size = wcs[i].byte_len - kUDHeaderSize - sizeof(RpcHeader);

        DLOG(INFO) << "Send buf " << cur_pair->send_buf << " with sequence " << cur_pair->send_buf->rpc_hdr.seq;

        RpcIdentifier &identifier = cur_pair->recv_buf->rpc_hdr.identifier;

        if (identifier.rpc_id == kRpcResponse) {
            // Client-side response. Fill in the recv_buf.
            auto seq = cur_pair->recv_buf->rpc_hdr.seq;
            MsgBufPair *pair = seq_buf_map[seq];
            assert(pair != nullptr);
            seq_buf_map.erase(seq);
            pair->recv_buf = cur_pair->recv_buf;
            DLOG(INFO) << "Cur pair " << cur_pair << " pair " << pair << " got response " << my_thread_id;
            pair->finished.store(true, std::memory_order_release);
        } else {
            // Server-side RPC.
            cur_pair->send_buf->rpc_hdr.seq = cur_pair->recv_buf->rpc_hdr.seq;
            cur_pair->send_buf->rpc_hdr.identifier = this->identifier;
            cur_pair->send_buf->rpc_hdr.identifier.rpc_id = kRpcResponse;
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

            auto handle = ReqHandle{ this, cur_pair, ah, qpn, ReqHandle::kQP };
            ctx->funcs[identifier.rpc_id](&handle, context);
        }
    }
}

void Rpc::handleSHMRequests() {
    while (shm_ring->serverTryRecv(shm_ticket)) {
        ShmRpcRingSlot *slot = shm_ring->get(shm_ticket);
        auto handle = ReqHandle{ this, &shm_bufs[shm_ticket % kRingElemCnt], nullptr, 0, ReqHandle::kSHM };
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
        DLOG(INFO) << "Response " << buf << " with sequence " << sbuf->rpc_hdr.seq;
        // sync -> batch, 800K -> 4M (reduce poll cq race)
        if (++rpc->send_cnt >= Rpc::kRecvWrGroupSize) {
            rpc->qp.send((uint64_t)&sbuf->rpc_hdr, sbuf->size + sizeof(RpcHeader), sbuf->lkey, ah, src_qp,
                         IBV_SEND_SIGNALED);
            ibv_wc wc;
            rpc->qp.pollSendCQ(1, &wc);
            rpc->send_cnt = 0;
        } else {
            rpc->qp.send((uint64_t)&sbuf->rpc_hdr, sbuf->size + sizeof(RpcHeader), sbuf->lkey, ah, src_qp, 0);
        }
    } else {
        assert(type == kSHM);
        rpc->shm_ring->serverSend(this->buf->ticket);
    }
}

}  // namespace rdma