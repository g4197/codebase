#include "rdma/context.h"

#include <tuple>

#include "rdma/qp.h"
#include "utils/log.h"

namespace rdma {

Context::Context(const std::string &rpc_ip, int rpc_port, uint8_t dev_port, int gid_index)
    : mr_on_chip_id(kMRIdOnChipStart), mgr(rpc_ip, rpc_port) {
    ibv_context *ib_ctx = nullptr;
    ibv_pd *ib_pd = nullptr;
    ibv_port_attr port_attr;

    int num_devices = 0;
    struct ibv_device **dev_list = ibv_get_device_list(&num_devices);

    // Traverse the device list
    int ports_to_discover = dev_port;

    for (int dev_i = 0; dev_i < num_devices; dev_i++) {
        ib_ctx = ibv_open_device(dev_list[dev_i]);
        if (!ib_ctx) {
            LOG(ERROR) << "Failed to open dev " + std::to_string(dev_i);
        }

        struct ibv_device_attr device_attr;
        memset(&device_attr, 0, sizeof(device_attr));
        if (ibv_query_device(ib_ctx, &device_attr) != 0) {
            LOG(ERROR) << "Failed to query device " << std::to_string(dev_i);
        }

        for (uint8_t port_i = 1; port_i <= device_attr.phys_port_cnt; port_i++) {
            // Count this port only if it is enabled
            if (ibv_query_port(ib_ctx, port_i, &port_attr) != 0) {
                LOG(ERROR) << "Failed to query port " << std::to_string(port_i) << " on device "
                           << ib_ctx->device->name;
            }

            if (port_attr.phys_state != IBV_PORT_ACTIVE && port_attr.phys_state != IBV_PORT_ACTIVE_DEFER) {
                continue;
            }

            if (ports_to_discover == 0) {
                LOG(INFO) << "Max gid: " << port_attr.gid_tbl_len;
                this->dev_index = dev_i;
                this->ctx = ib_ctx;
                this->port = port_i;
                goto finish_query_port;
            }

            ports_to_discover--;
        }

        if (ibv_close_device(ib_ctx) != 0) {
            LOG(ERROR) << "Failed to close device " << ib_ctx->device->name;
        }
    }
finish_query_port:
    ibv_free_device_list(dev_list);

    // allocate protection domain
    ib_pd = ibv_alloc_pd(ctx);
    if (!ib_pd) {
        LOG(ERROR) << "ibv_alloc_pd failed";
    }

    this->pd = ib_pd;
    this->gid_index = gid_index;
    this->lid = port_attr.lid;
    ibv_query_gid(ctx, port, gid_index, &this->gid);

    // check device memory support
    checkDMSupported();
}

void Context::checkDMSupported() {
#ifdef NO_EX_VERBS
    return;
#else
    ibv_query_device_ex_input input;
    ibv_device_attr_ex attrs;
    memset(&input, 0, sizeof(input));
    memset(&attrs, 0, sizeof(attrs));
    if (ibv_query_device_ex(ctx, &input, &attrs)) {
        LOG(ERROR) << "Couldn't query device attributes";
    }
    device_memory_size = attrs.max_dm_size;
    LOG(INFO) << "NIC Device Memory is " << device_memory_size / 1024 << "KB";
#endif
}

Context::~Context() {}

ibv_mr *Context::createMR(void *addr, uint64_t size, bool on_chip, bool odp, bool mw_binding) {
    // If not on_chip, addr should be a pre-allocated buffer.
    // odp and mw_binding are suitable for not on-chip memory.
    if (!on_chip) {
        int flag = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC;
        if (odp) flag |= IBV_ACCESS_ON_DEMAND;
        if (mw_binding) flag |= IBV_ACCESS_MW_BIND;
        ibv_mr *mr = ibv_reg_mr(pd, (void *)addr, size, flag);

        if (!mr) LOG(ERROR) << "Memory registration failed";
        mgr.putMRInfo(mr_id++, MRInfo{ true, true, mr->rkey, (uint64_t)mr->addr, mr->length });
        return mr;
    } else {
        return createMROnChip(addr, size);
    }
}

ibv_mr *Context::createMROnChip(void *addr, uint64_t size) {
#ifdef NO_EX_VERBS
    return nullptr;
#else
    std::ignore = addr;
    ibv_alloc_dm_attr dm_attr;
    memset(&dm_attr, 0, sizeof(ibv_alloc_dm_attr));
    dm_attr.length = size;
    ibv_dm *dm = ibv_alloc_dm(ctx, &dm_attr);
    rt_assert_ptr(dm, "ibv_alloc_dm failed");

    ibv_mr *mr = ibv_reg_dm_mr(pd, dm, 0, size,
                               IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE |
                                   IBV_ACCESS_REMOTE_ATOMIC | IBV_ACCESS_ZERO_BASED);

    rt_assert_ptr(mr, "ibv_reg_dm_mr failed " + std::string(strerror(errno)));

    char *buf = (char *)malloc(size);
    memset(buf, 0, size);
    ibv_memcpy_to_dm(dm, 0, buf, 0);
    free(buf);
    mgr.putMRInfo(mr_on_chip_id++, MRInfo{ true, true, mr->rkey, (uint64_t)mr->addr, mr->length });
    return mr;
#endif  // NO_EX_VERBS
}

ibv_cq *Context::createCQ(int cqe, void *cq_ctx, ibv_comp_channel *channel) {
    // if ctx != nullptr, then it will be in ret->context
    // if channel != nullptr, then event-based things will be used.
    // create a full-functional cq.
    ibv_cq *ret = ibv_create_cq(this->ctx, cqe, cq_ctx, channel, 0);
    rt_assert_ptr(ret, "Create CQ error");
    return ret;
}

ibv_srq *Context::createSRQ(int queue_depth, int sgl_size) {
    // shared receive queue, can be used by multiple QPs.
    ibv_srq_init_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.attr.max_wr = queue_depth;
    attr.attr.max_sge = sgl_size;
    ibv_srq *ret = ibv_create_srq(pd, &attr);
    rt_assert_ptr(ret, "Create SRQ error");
    return ret;
}

QP Context::createQP(ibv_qp_type mode, ibv_cq *send_cq, ibv_cq *recv_cq, ibv_srq *srq, int queue_depth, int sgl_size,
                     uint32_t max_inline_data) {
    ibv_qp_init_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_type = mode;
    attr.sq_sig_all = 0;
    attr.send_cq = send_cq;
    attr.recv_cq = recv_cq;
    attr.srq = srq;

    attr.cap.max_send_wr = queue_depth;
    attr.cap.max_recv_wr = queue_depth;
    attr.cap.max_send_sge = sgl_size;
    attr.cap.max_recv_sge = sgl_size;
    attr.cap.max_inline_data = max_inline_data;
    ibv_qp *qp = ibv_create_qp(pd, &attr);
    if (qp == nullptr) {
        LOG(ERROR) << "Create QP error: " << strerror(errno);
    }

    int id = qp_id++;
    QP ret = QP(qp, this, id);

    QPInfo info;
    info.valid = true;
    info.qpn = qp->qp_num;
    info.lid = this->lid;
    memcpy(info.gid, this->gid.raw, 16);
    mgr.putQPInfo(id, info);

    return ret;
}

QP Context::createQP(ibv_qp_type mode, ibv_cq *cq, ibv_srq *srq, int queue_depth, int sgl_size,
                     uint32_t max_inline_data) {
    return createQP(mode, cq, cq, srq, queue_depth, sgl_size, max_inline_data);
}

void Context::printDeviceInfoEx() {
#ifdef NO_EX_VERBS
    return;
#else
    ibv_query_device_ex_input input;
    ibv_device_attr_ex attrs;
    memset(&input, 0, sizeof(input));
    memset(&attrs, 0, sizeof(attrs));
    ibv_query_device_ex(ctx, &input, &attrs);

    // original attr
    LOG(INFO) << "Device name: " << ctx->device->name;
    LOG(INFO) << "Phys port cnt: " << (int)attrs.orig_attr.phys_port_cnt;
    LOG(INFO) << "Max mr size: " << attrs.orig_attr.max_mr_size;
    LOG(INFO) << "Max qp: " << attrs.orig_attr.max_qp;
    LOG(INFO) << "Max cq: " << attrs.orig_attr.max_cq;
    LOG(INFO) << "Max mr: " << attrs.orig_attr.max_mr;
    LOG(INFO) << "Max mw: " << attrs.orig_attr.max_mw;
    LOG(INFO) << "Max pd: " << attrs.orig_attr.max_pd;
    LOG(INFO) << "Max sge: " << attrs.orig_attr.max_sge;
    LOG(INFO) << "Atomic capability: " << attrs.orig_attr.atomic_cap << " (0: none, 1: in HCA, 2: global)";

    // external attr
    LOG(INFO) << "Completion timestamp mask: " << attrs.completion_timestamp_mask;
    LOG(INFO) << "HCA core clock: " << attrs.hca_core_clock << " kHz";
    LOG(INFO) << "Device cap flags ex: " << attrs.device_cap_flags_ex;
    LOG(INFO) << "Raw packet caps: " << attrs.raw_packet_caps;
    LOG(INFO) << "Max device memory size: " << attrs.max_dm_size / 1024 << " KB";
    LOG(INFO) << "PCI atomic ops: SWP " << attrs.pci_atomic_caps.swap << " CAS " << attrs.pci_atomic_caps.compare_swap
              << " FAA " << attrs.pci_atomic_caps.fetch_add << " (1 << i means supporting (4 * 2i) bytes)";
    LOG(INFO) << "On-Demand Paging caps: " << attrs.odp_caps.general_caps;
    LOG(INFO) << "Tag matching Max rendezvous header: " << attrs.tm_caps.max_rndv_hdr_size;
    LOG(INFO) << "Tag matching Max number of outstanding operations: " << attrs.tm_caps.max_ops;
    LOG(INFO) << "Tag matching Max number of tags: " << attrs.tm_caps.max_num_tags;
#endif
}

void Context::fillAhAttr(ibv_ah_attr *attr, uint32_t remote_lid, const uint8_t *remote_gid) {
    memset(attr, 0, sizeof(ibv_ah_attr));
    attr->dlid = remote_lid;
    attr->sl = 0;
    attr->src_path_bits = 0;
    attr->port_num = port;

    // fill ah_attr with GRH
    attr->is_global = 1;
    memcpy(&attr->grh.dgid, remote_gid, 16);
    attr->grh.flow_label = 0;
    attr->grh.hop_limit = 1;  // Won't leave the local subnet
    attr->grh.sgid_index = gid_index;
    attr->grh.traffic_class = 0;
}

QPInfo Context::getQPInfo(const std::string &ctx_ip, int ctx_port, int qp_id) {
    auto ip_port_pair = ctx_ip + ":" + std::to_string(ctx_port);
    if (!mgr_clients.exists(ip_port_pair)) {
        std::lock_guard<std::mutex> lock(mgr_clients_mutex);
        if (!mgr_clients.exists(ip_port_pair)) {
            mgr_clients.put(ip_port_pair, ManagerClient(ctx_ip, ctx_port));
        }
    }
    return mgr_clients.get(ip_port_pair).getQPInfo(qp_id);
}

MRInfo Context::getMRInfo(const std::string &ctx_ip, int ctx_port, int mr_id) {
    auto ip_port_pair = ctx_ip + ":" + std::to_string(ctx_port);
    if (!mgr_clients.exists(ip_port_pair)) {
        std::lock_guard<std::mutex> lock(mgr_clients_mutex);
        if (!mgr_clients.exists(ip_port_pair)) {
            mgr_clients.put(ip_port_pair, ManagerClient(ctx_ip, ctx_port));
        }
    }
    return mgr_clients.get(ip_port_pair).getMRInfo(mr_id);
}

};  // namespace rdma