#include "rdma/qp.h"

#include "rdma/context.h"

namespace rdma {

inline void fillSge(ibv_sge &sg, uint64_t source, uint64_t size, uint32_t lkey) {
    memset(&sg, 0, sizeof(sg));
    sg.addr = (uintptr_t)source;
    sg.length = size;
    sg.lkey = lkey;
}

inline void fillSge(ibv_sge &sg, ibv_recv_wr &wr, uint64_t source, uint64_t size, uint32_t lkey) {
    memset(&sg, 0, sizeof(sg));
    sg.addr = (uintptr_t)source;
    sg.length = size;
    sg.lkey = lkey;
}

QP::QP() {}

QP::QP(ibv_qp *qp, Context *ctx, int id) : qp(qp), ctx(ctx), id(id) {
    info.valid = true;
    info.qpn = qp->qp_num;
    info.lid = ctx->lid;
    memcpy(info.gid, ctx->gid.raw, sizeof(ibv_gid));

    // Initialize sge and wr.
    memset(send_sge, 0, sizeof(send_sge));
    memset(send_wr, 0, sizeof(send_wr));
    memset(recv_sge, 0, sizeof(recv_sge));
    memset(recv_wr, 0, sizeof(recv_wr));
    for (int i = 0; i < kSendOpsCnt; ++i) {
        send_wr[i].wr_id = 0;
        send_wr[i].sg_list = &send_sge[i];
        send_wr[i].num_sge = 1;
    }

    for (int i = 0; i < kRecvOpsCnt; ++i) {
        recv_wr[i].wr_id = 0;
        recv_wr[i].sg_list = &recv_sge[i];
        recv_wr[i].num_sge = 1;
    }
}

bool QP::connect(const std::string &ctx_ip, int ctx_port, int qp_id) {
    if (qp->qp_type == IBV_QPT_UD) {
        return modifyToRTS(false);
    } else {
        QPInfo qp_info;
        memset(&qp_info, 0, sizeof(qp_info));
        while (!qp_info.valid) {
            qp_info = ctx->getQPInfo(ctx_ip, ctx_port, qp_id);
        }
        return modifyToRTR(qp_info) && modifyToRTS(false);
    }
}

bool QP::modifyToRTR(const QPInfo &remote_qp_info) {
    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));

    attr.qp_state = IBV_QPS_INIT;
    attr.port_num = ctx->port;
    attr.pkey_index = 0;

    switch (qp->qp_type) {
        case IBV_QPT_RC: {
            attr.qp_access_flags = IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC;
            break;
        }
        case IBV_QPT_UC: {
            attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE;
            break;
        }
        default: {
            LOG(ERROR) << "Unknown QP type";
            return false;
        }
    }

    if (ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS)) {
        LOG(ERROR) << "Failed to modify QP state to INIT";
        return false;
    }

    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = IBV_MTU_4096;
    attr.dest_qp_num = remote_qp_info.qpn;
    attr.rq_psn = kPSN;

    ctx->fillAhAttr(&attr.ah_attr, remote_qp_info.lid, remote_qp_info.gid);

    int flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN;

    if (qp->qp_type == IBV_QPT_RC) {
        attr.max_dest_rd_atomic = 16;
        attr.min_rnr_timer = 12;
        flags |= IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
    }

    if (ibv_modify_qp(qp, &attr, flags)) {
        LOG(ERROR) << "failed to modify QP state to RTR";
        return false;
    }
    DLOG(INFO) << "QP state is now RTR";
    return true;
}

bool QP::modifyToRTS(bool rnr_retry) {
    if (this->qp->qp_type == IBV_QPT_UD) {
        DLOG(INFO) << "Modify UD to RTS";
        struct ibv_qp_attr attr;
        memset(&attr, 0, sizeof(attr));

        attr.qp_state = IBV_QPS_INIT;
        attr.pkey_index = 0;
        attr.port_num = ctx->port;
        attr.qkey = kUDQkey;

        if (ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_QKEY)) {
            LOG(ERROR) << "Failed to modify QP state to INIT";
            return false;
        }

        memset(&attr, 0, sizeof(attr));
        attr.qp_state = IBV_QPS_RTR;
        if (ibv_modify_qp(qp, &attr, IBV_QP_STATE)) {
            LOG(ERROR) << "failed to modify QP state to RTR";
            return false;
        }

        memset(&attr, 0, sizeof(attr));
        attr.qp_state = IBV_QPS_RTS;
        attr.sq_psn = kPSN;

        if (ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_SQ_PSN)) {
            LOG(ERROR) << "failed to modify QP state to RTS";
            return false;
        }
    } else if (this->qp->qp_type == IBV_QPT_RC || this->qp->qp_type == IBV_QPT_UC) {
        struct ibv_qp_attr attr;
        int flags;
        memset(&attr, 0, sizeof(attr));

        attr.qp_state = IBV_QPS_RTS;
        attr.sq_psn = kPSN;
        flags = IBV_QP_STATE | IBV_QP_SQ_PSN;

        if (qp->qp_type == IBV_QPT_RC) {
            attr.timeout = 14;  // 0.0671s
            if (rnr_retry) {
                attr.retry_cnt = 7;
                attr.rnr_retry = 7;
            }
            attr.max_rd_atomic = 16;
            flags |= IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_MAX_QP_RD_ATOMIC;
        }

        if (ibv_modify_qp(qp, &attr, flags)) {
            LOG(ERROR) << "failed to modify QP state to RTS";
            return false;
        }
    }
    DLOG(INFO) << "QP state is now RTS";
    return true;
}

bool QP::send(uint64_t source, uint64_t size, uint32_t lkey, ibv_ah *ah, uint32_t remote_qpn, uint64_t send_flags,
              bool with_imm, int32_t imm, uint64_t wr_id) {
    assert(qp->qp_type == IBV_QPT_UD);
    DLOG(INFO) << "UD post send";
    ibv_sge &sg = with_imm ? send_sge[kSendWithImm] : send_sge[kSend];
    ibv_send_wr &wr = with_imm ? send_wr[kSendWithImm] : send_wr[kSend];
    ibv_send_wr *bad_wr;
    fillSge(sg, source, size, lkey);

    wr.imm_data = imm;
    wr.wr_id = wr_id;
    wr.wr.ud.ah = ah;
    wr.wr.ud.remote_qpn = remote_qpn;
    wr.wr.ud.remote_qkey = kUDQkey;
    wr.send_flags = send_flags;

    int ret = ibv_post_send(qp, &wr, &bad_wr);
    if (ret) {
        LOG(ERROR) << "Send with RDMA_SEND failed " << strerror(ret);
        return false;
    }
    DLOG(INFO) << "UD post send finished";
    return true;
}

bool QP::send(uint64_t source, uint64_t size, uint32_t lkey, bool with_imm, int32_t imm) {
    assert(qp->qp_type == IBV_QPT_RC || qp->qp_type == IBV_QPT_UC);
    ibv_sge &sg = with_imm ? send_sge[kSendWithImm] : send_sge[kSend];
    ibv_send_wr &wr = with_imm ? send_wr[kSendWithImm] : send_wr[kSend];
    ibv_send_wr *bad_wr;

    fillSge(sg, source, size, lkey);
    wr.imm_data = imm;
    wr.send_flags = IBV_SEND_SIGNALED;
    if (ibv_post_send(qp, &wr, &bad_wr)) {
        LOG(ERROR) << "Send with RDMA_SEND failed";
        return false;
    }
    return true;
}

bool QP::recv(uint64_t source, uint64_t size, uint32_t lkey, uint64_t wr_id, bool is_srq) {
    DLOG(INFO) << "Post recv " << source << " " << size << " " << lkey;
    ibv_sge &sg = recv_sge[kRecv];
    ibv_recv_wr &wr = recv_wr[kRecv];
    ibv_recv_wr *bad_wr;

    fillSge(sg, source, size, lkey);
    int ret = 0;
    if (is_srq) {
        ret = ibv_post_srq_recv(qp->srq, &wr, &bad_wr);
    } else {
        wr.wr_id = wr_id;
        ret = ibv_post_recv(qp, &wr, &bad_wr);
    }
    if (ret) {
        LOG(ERROR) << "Recv with RDMA_RECV failed " << strerror(ret);
        return false;
    }
    return true;
}

bool QP::read(uint64_t source, uint64_t dest, uint64_t size, uint32_t lkey, uint32_t rkey, uint64_t send_flags,
              uint64_t wr_id) {
    assert(qp->qp_type == IBV_QPT_RC || qp->qp_type == IBV_QPT_UC);
    ibv_sge &sg = send_sge[kRead];
    ibv_send_wr &wr = send_wr[kRead];
    ibv_send_wr *bad_wr;

    fillSge(sg, source, size, lkey);
    wr.wr.rdma.remote_addr = dest;
    wr.wr.rdma.rkey = rkey;
    wr.send_flags = send_flags;
    wr.wr_id = wr_id;

    if (ibv_post_send(qp, &wr, &bad_wr)) {
        LOG(ERROR) << "Send with RDMA_READ failed";
        return false;
    }
    return true;
}

bool QP::write(uint64_t source, uint64_t dest, uint64_t size, uint32_t lkey, uint32_t rkey, uint64_t send_flags,
               uint64_t wr_id) {
    assert(qp->qp_type == IBV_QPT_RC || qp->qp_type == IBV_QPT_UC);
    ibv_sge &sg = send_sge[kWrite];
    ibv_send_wr &wr = send_wr[kWrite];
    ibv_send_wr *bad_wr;

    fillSge(sg, source, size, lkey);
    wr.wr.rdma.remote_addr = dest;
    wr.wr.rdma.rkey = rkey;
    wr.send_flags = send_flags;
    wr.wr_id = wr_id;

    if (ibv_post_send(qp, &wr, &bad_wr)) {
        LOG(ERROR) << "Send with RDMA_READ failed";
        return false;
    }
    return true;
}

bool QP::faa(uint64_t source, uint64_t dest, uint64_t delta, uint32_t lkey, uint32_t rkey) {
    assert(qp->qp_type == IBV_QPT_RC || qp->qp_type == IBV_QPT_UC);
    ibv_sge &sg = send_sge[kFAA];
    ibv_send_wr &wr = send_wr[kFAA];
    ibv_send_wr *bad_wr;

    fillSge(sg, source, sizeof(uint64_t), lkey);
    wr.wr.atomic.remote_addr = dest;
    wr.wr.atomic.rkey = rkey;
    wr.wr.atomic.compare_add = delta;
    wr.send_flags = IBV_SEND_SIGNALED;

    if (ibv_post_send(qp, &wr, &bad_wr)) {
        LOG(ERROR) << "Send with RDMA_READ failed";
        return false;
    }
    return true;
}

bool QP::cas(uint64_t source, uint64_t dest, uint64_t compare, uint64_t swap, uint32_t lkey, uint32_t rkey,
             uint64_t send_flags, uint64_t wr_id) {
    assert(qp->qp_type == IBV_QPT_RC || qp->qp_type == IBV_QPT_UC);
    ibv_sge &sg = send_sge[kCAS];
    ibv_send_wr &wr = send_wr[kCAS];
    ibv_send_wr *bad_wr;

    fillSge(sg, source, sizeof(uint64_t), lkey);
    wr.wr.atomic.remote_addr = dest;
    wr.wr.atomic.rkey = rkey;
    wr.wr.atomic.compare_add = compare;
    wr.wr.atomic.swap = swap;
    wr.send_flags = send_flags;
    wr.wr_id = wr_id;

    if (ibv_post_send(qp, &wr, &bad_wr)) {
        LOG(ERROR) << "Send with RDMA_READ failed";
        return false;
    }
    return true;
}

void QP::printState() {
    struct ibv_qp_attr attr;
    struct ibv_qp_init_attr init_attr;
    ibv_query_qp(qp, &attr, IBV_QP_STATE, &init_attr);
    switch (attr.qp_state) {
        case IBV_QPS_RESET: {
            LOG(INFO) << "QP state: IBV_QPS_RESET";
            break;
        }
        case IBV_QPS_INIT: {
            LOG(INFO) << "QP state: IBV_QPS_INIT";
            break;
        }
        case IBV_QPS_RTR: {
            LOG(INFO) << "QP state: IBV_QPS_RTR";
            break;
        }
        case IBV_QPS_RTS: {
            LOG(INFO) << "QP state: IBV_QPS_RTS";
            break;
        }
        case IBV_QPS_SQD: {
            LOG(INFO) << "QP state: IBV_QPS_SQD";
            break;
        }
        case IBV_QPS_SQE: {
            LOG(INFO) << "QP state: IBV_QPS_SQE";
            break;
        }
        case IBV_QPS_ERR: {
            LOG(INFO) << "QP state: IBV_QPS_ERR";
            break;
        }
        case IBV_QPS_UNKNOWN: {
            LOG(INFO) << "QP state: IBV_QPS_UNKNOWN";
            break;
        }
        default: {
            LOG(INFO) << "QP state: unknown";
            break;
        }
    }
}

void QP::pollSendCQ(int num_entries, ibv_wc *wc) {
    int cnt = 0;
    do {
        cnt += ibv_poll_cq(qp->send_cq, num_entries, wc);
    } while (cnt < num_entries);

    if (wc->status != IBV_WC_SUCCESS) {
        LOG(ERROR) << "Send CQ completion with error: " << wc->status << " (" << ibv_wc_status_str(wc->status) << ")";
    }
}

void QP::pollRecvCQ(int num_entries, ibv_wc *wc) {
    DLOG(INFO) << "Poll recv CQ";
    int cnt = 0;
    do {
        cnt += ibv_poll_cq(qp->recv_cq, num_entries, wc);
    } while (cnt < num_entries);

    if (wc->status != IBV_WC_SUCCESS) {
        LOG(ERROR) << "Recv CQ completion with error: " << wc->status << " (" << ibv_wc_status_str(wc->status) << ")";
    }
}

}  // namespace rdma