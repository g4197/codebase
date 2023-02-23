#include "rdma/rpc.h"

namespace rdma {
MsgBuf *RpcContext::allocBuf() {
    MsgBuf *buf = new MsgBuf(this);
    return buf;
}

Rpc::Rpc(RpcContext *rpc_ctx, void *context, int rpc_id) : ctx(rpc_ctx), context(context) {
    ibv_cq *send_cq = rpc_ctx->ctx.createCQ();
    ibv_cq *recv_cq = rpc_ctx->ctx.createCQ();
    qp = rpc_ctx->ctx.createQP(rpc_id, IBV_QPT_UD, send_cq, recv_cq);
    qp.modifyToRTS(false);

    if (ctx->is_server) {
        DLOG(INFO) << "Is server, allocating buffers";
        if (posix_memalign((void **)&srv_bufs, kMsgBufAlign, sizeof(MsgBuf) * kSrvBufCnt)) {
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
    }
}

RpcSession Rpc::connect(const std::string &ctx_ip, int ctx_port, int rpc_id) {
    assert(!ctx->is_server);
    RpcSession session;
    QPInfo qp_info = ctx->ctx.getQPInfo(ctx_ip, ctx_port, rpc_id);
    ibv_ah_attr ah_attr;
    ctx->ctx.fillAhAttr(&ah_attr, qp_info);
    session.ah = ibv_create_ah(ctx->ctx.pd, &ah_attr);
    session.qpn = qp_info.qpn;
    return session;
}

void Rpc::send(RpcSession *session, uint8_t rpc_id, MsgBufPair *buf) {
    assert(!ctx->is_server);
    auto &sbuf = buf->send_buf;
    auto &rbuf = buf->recv_buf;
    qp.recv((uint64_t)rbuf->hdr, kMTU, rbuf->lkey, (uint64_t)buf);

    // batch
    if (++send_cnt == Context::kQueueDepth) {
        qp.send((uint64_t)sbuf->buf, sbuf->size, sbuf->lkey, session->ah, session->qpn, IBV_SEND_SIGNALED, true, rpc_id,
                (uint64_t)buf);
        qp.pollSendCQ(1, wc);
        send_cnt = 0;
    } else {
        qp.send((uint64_t)sbuf->buf, sbuf->size, sbuf->lkey, session->ah, session->qpn, 0, true, rpc_id, (uint64_t)buf);
    }
}

void Rpc::runClientLoopOnce() {
    assert(!ctx->is_server);
    // poll recv CQ.
    int finished = ibv_poll_cq(qp.qp->recv_cq, Context::kQueueDepth, wc);
    for (int i = 0; i < finished; ++i) {
        MsgBufPair *cur_pair = (MsgBufPair *)wc[i].wr_id;
        cur_pair->recv_buf->size = wc[i].byte_len - kUDHeaderSize;
        cur_pair->finished = true;
    }
}

bool Rpc::tryRecv(MsgBufPair *msg) {
    assert(!ctx->is_server);
    runClientLoopOnce();
    return msg->finished;
}

void Rpc::recv(MsgBufPair *msg) {
    assert(!ctx->is_server);
    while (!msg->finished) {
        runClientLoopOnce();
    }
    msg->finished = false;
}

void Rpc::runServerLoopOnce() {
    assert(ctx->is_server);
    int finished = ibv_poll_cq(qp.qp->recv_cq, Context::kQueueDepth, wc);
    for (int i = 0; i < finished; ++i) {
        MsgBufPair *cur_pair = (MsgBufPair *)wc[i].wr_id;
        cur_pair->recv_buf->size = wc[i].byte_len - kUDHeaderSize;
        uint8_t rpc_id = wc[i].imm_data;

        // Ref: https://enterprise-support.nvidia.com/s/article/lrh-and-grh-infiniband-headers
        ibv_grh *grh = (ibv_grh *)cur_pair->recv_buf->hdr;
        ibv_ah *ah;
        if (gid_ah_map.find(grh->sgid) == gid_ah_map.end()) {
            ah = ibv_create_ah_from_wc(ctx->ctx.pd, wc, grh, ctx->ctx.port);
            gid_ah_map[grh->sgid] = ah;
        } else {
            ah = gid_ah_map[grh->sgid];
        }

        ReqHandle handle{ this, cur_pair, ah, wc[i].src_qp };
        ctx->funcs[rpc_id](&handle, context);
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

void RpcSession::recv(MsgBufPair *msg) {
    rpc->recv(msg);
}

void ReqHandle::response() {
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
}

}  // namespace rdma