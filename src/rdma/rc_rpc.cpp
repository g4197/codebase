#include "rdma/rpc/rc_rpc.h"

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

Rpc::Rpc(RpcContext *rpc_ctx, void *context, int rpc_id) : ctx(rpc_ctx), context(context), conn_buf(rpc_ctx) {
    cur_group = 0;
    send_cnt = 0;
    this->send_cq = rpc_ctx->ctx.createCQ();
    this->recv_cq = rpc_ctx->ctx.createCQ();

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
                srv_recv_sges[i].addr = (uint64_t)srv_bufs[i].recv_buf->buf;
                srv_recv_sges[i].length = kMTU;
                srv_recv_sges[i].lkey = srv_bufs[i].recv_buf->lkey;
                srv_recv_wrs[i].wr_id = (uint64_t)&srv_bufs[i];
                srv_recv_wrs[i].next = i == end - 1 ? nullptr : &srv_recv_wrs[i + 1];
                srv_recv_wrs[i].num_sge = 1;
                srv_recv_wrs[i].sg_list = &srv_recv_sges[i];
            }
            srv_recv_wr_hdrs[gi] = &srv_recv_wrs[start];
        }

        DLOG(INFO) << "Initialize shm buffer";
        shm_ring = ShmRpcRing::create(shm_key(ctx->my_ip, ctx->my_port, rpc_id), kRingElemCnt);
        if (posix_memalign((void **)&shm_bufs, kCacheLineSize, sizeof(MsgBuf) * kRingElemCnt)) {
            LOG(FATAL) << "Failed to allocate memory for shm buffers";
        }

        for (int i = 0; i < kRingElemCnt; ++i) {
            new (shm_bufs + i) MsgBufPair(shm_ring->get(i), i);
        }

        for (int i = 0; i < Context::kQueueDepth; ++i) {
            req_handle_free_queue.push(new ReqHandle);
        }

        this->srv_srq = ctx->ctx.createSRQ(kSrvBufCnt);

        postNextGroupRecv();
        postNextGroupRecv();

        ctx->ctx.mgr->srv_.bind("connect_" + std::to_string(rpc_id), [this](std::string ip, int port, int qp_id) {
            LOG(INFO) << "conn " << ip << ":" << port << " " << qp_id;
            auto qp = new QP;
            *qp = this->ctx->ctx.createQP(IBV_QPT_RC, this->send_cq, this->recv_cq, this->srv_srq);
            this->qpn_qp_map[qp->qp->qp_num] = { qp, 0, ip, port, qp_id };
            qp->connect(ip, port, qp_id);
            return qp->id;
        });

        LOG(INFO) << ctx->my_ip << ":" << ctx->my_port << " create rpc id " << rpc_id;
    }
}

RpcSession Rpc::connect(const std::string &ctx_ip, int ctx_port, int rmt_rpc_id) {
    assert(!ctx->is_server);
    RpcSession session;
    memset(&session, 0, sizeof(RpcSession));

    session.rpc = this;
    if (ctx_ip == this->ctx->my_ip) {
        // Same machine, use shared memory.
        session.shm_ring = ShmRpcRing::open(shm_key(ctx_ip, ctx_port, rmt_rpc_id), kRingElemCnt);
    } else {
        ibv_cq *send_cq = ctx->ctx.createCQ();
        ibv_cq *recv_cq = ctx->ctx.createCQ();
        session.qp = { new QP, 0, ctx_ip, ctx_port, rmt_rpc_id };
        *(session.qp.qp) = ctx->ctx.createQP(IBV_QPT_RC, send_cq, recv_cq);
        // Exchange connection info.
        int rmt_qp_id =
            ctx->ctx.connect(ctx_ip, ctx_port)
                ->cli_->call("connect_" + std::to_string(rmt_rpc_id), ctx->my_ip, ctx->my_port, session.qp.qp->id)
                .as<int>();  // opposite connect me.
        session.qp.qp->connect(ctx_ip, ctx_port, rmt_qp_id);
        LOG(INFO) << "my ip: " << this->ctx->my_ip << ":" << ctx->my_port << " id " << session.qp.qp->id
                  << " connect to " << ctx_ip << ":" << ctx_port << " rpc id " << rmt_rpc_id;
        session.rmt_qp_id = rmt_qp_id;
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
        session->qp.qp->recv((uint64_t)rbuf->buf, kMTU, rbuf->lkey, (uint64_t)buf);

        // batch
        if (++session->qp.send_cnt == 1) {
            session->qp.qp->send((uint64_t)sbuf->buf, sbuf->size, sbuf->lkey, IBV_SEND_SIGNALED, true, rpc_id,
                                 (uint64_t)buf);
            if (!session->qp.qp->pollSendCQ(1, wcs)) {
                LOG(INFO) << this->ctx->my_ip << ":" << this->ctx->my_port << " send to " << session->qp.oppo_ip << ":"
                          << session->qp.oppo_port << " " << session->qp.oppo_id << " failed";
            }
            session->qp.send_cnt = 0;
        } else {
            session->qp.qp->send((uint64_t)sbuf->buf, sbuf->size, sbuf->lkey, 0, true, rpc_id, (uint64_t)buf);
        }
    }
}

bool Rpc::recv(MsgBufPair *msg, size_t retry_times) {
    return msg->session->recv(msg, retry_times);
}

void Rpc::runServerLoopOnce() {
    assert(ctx->is_server);
    handleQPRequests();
    handleSHMRequests();
}

void Rpc::handleQPRequests() {
    int finished = ibv_poll_cq(this->recv_cq, Context::kQueueDepth, wcs);
    for (int i = 0; i < finished; ++i) {
        MsgBufPair *cur_pair = (MsgBufPair *)wcs[i].wr_id;
        cur_pair->recv_buf->size = wcs[i].byte_len;
        QPCnt &qp_cnt = qpn_qp_map[wcs[i].qp_num];
        uint8_t rpc_id = wcs[i].imm_data;
        DLOG(INFO) << "Got rpc " << (int)rpc_id << " from " << qp_cnt.oppo_ip << ":" << qp_cnt.oppo_port << " "
                   << qp_cnt.oppo_id;

        auto handle = req_handle_free_queue.pop();
        *handle = ReqHandle{ this, cur_pair, &qp_cnt, ReqHandle::kQP };
        ctx->funcs[rpc_id](handle, context);
    }
}

void Rpc::handleSHMRequests() {
    while (shm_ring->serverTryRecv(shm_ticket)) {
        ShmRpcRingSlot *slot = shm_ring->get(shm_ticket);

        auto handle = req_handle_free_queue.pop();
        *handle = ReqHandle{ this, &shm_bufs[shm_ticket % kRingElemCnt], nullptr, ReqHandle::kSHM };
        ctx->funcs[slot->rpc_id](handle, context);
        ++shm_ticket;
    }
}

void Rpc::postNextGroupRecv() {
    DLOG(INFO) << "postNextGroupRecv " << cur_group;
    ibv_recv_wr *bad_wr;
    ibv_post_srq_recv(this->srv_srq, srv_recv_wr_hdrs[cur_group], &bad_wr);
    cur_group = (cur_group + 1) % kRecvWrGroupCnt;
}

void RpcSession::send(uint8_t rpc_id, MsgBufPair *buf) {
    rpc->send(this, rpc_id, buf);
}

void RpcSession::handleQPResponses() {
    // poll recv CQ.
    if (qp.qp != nullptr) {
        auto &wcs = rpc->wcs;
        int finished = ibv_poll_cq(qp.qp->qp->recv_cq, Context::kQueueDepth, wcs);
        for (int i = 0; i < finished; ++i) {
            MsgBufPair *cur_pair = (MsgBufPair *)wcs[i].wr_id;
            cur_pair->recv_buf->size = wcs[i].byte_len;
            cur_pair->finished.store(true, std::memory_order_release);
        }
    }
}

void RpcSession::handleSHMResponses() {
    if (shm_ring != nullptr) {
        for (auto it = tickets.begin(); it != tickets.end();) {
            auto &p = *it;
            if (shm_ring->clientTryRecv(p.second->recv_buf, p.first)) {
                p.second->finished.store(true, std::memory_order_release);
                it = tickets.erase(it);
            } else {
                ++it;
            }
        }
    }
}

bool RpcSession::recv(MsgBufPair *msg, size_t retry_times) {
    assert(!rpc->ctx->is_server);
    for (size_t i = 0; i < retry_times; ++i) {
        if (msg->finished) {
            msg->finished = false;
            return true;
        }
        handleQPResponses();
        handleSHMResponses();
    }
    LOG(INFO) << "Possible packet loss... ip " << rpc->ctx->my_ip << " to " << msg->session->qp.oppo_ip
              << this->rmt_qp_id;
    return false;
}

void ReqHandle::response() {
    if (type == kQP) {
        auto &sbuf = buf->send_buf;
        // sync -> batch, 800K -> 4M (reduce poll cq race)
        QP *cur_qp = qp->qp;
        if (++rpc->send_cnt >= Rpc::kRecvWrGroupSize) {
            rpc->postNextGroupRecv();
            rpc->send_cnt = 0;
        }

        if (++qp->send_cnt >= Context::kQueueDepth) {
            cur_qp->send((uint64_t)sbuf->buf, sbuf->size, sbuf->lkey, IBV_SEND_SIGNALED);
            ibv_wc wc;
            cur_qp->pollSendCQ(1, &wc);
            qp->send_cnt = 0;
        } else {
            cur_qp->send((uint64_t)sbuf->buf, sbuf->size, sbuf->lkey, 0);
        }
    } else {
        assert(type == kSHM);
        rpc->shm_ring->serverSend(this->buf->ticket);
    }
    rpc->req_handle_free_queue.push(this);
}

}  // namespace rdma
