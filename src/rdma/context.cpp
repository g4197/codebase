#include "rdma/context.h"

#include <glog/logging.h>

#include "rdma/qp.h"

namespace rdma {
Context::Context(int dev_index, uint8_t port, int gid_index, const std::string &conf_path) {
    this->memcache = new Memcache(conf_path);
    ibv_device *dev = nullptr;
    ibv_context *ctx = nullptr;
    ibv_pd *pd = nullptr;
    ibv_port_attr port_attr;

    // get device names in the system
    int device_num;
    struct ibv_device **device_list = ibv_get_device_list(&device_num);
    if (!device_list) {
        LOG(ERROR) << "failed to get IB devices list";
        goto CreateResourcesError;
    }

    // if there isn't any IB device in host
    if (!device_num) {
        LOG(INFO) << "No IB device found!";
        goto CreateResourcesError;
    }

    if (dev_index >= device_num) {
        LOG(INFO) << "Dev index is out of range!";
        goto CreateResourcesError;
    }

    dev = device_list[dev_index];

    // get device handle
    ctx = ibv_open_device(dev);
    if (!ctx) {
        LOG(ERROR) << "failed to open device";
        goto CreateResourcesError;
    }
    /* We are now done with device list, free it */
    ibv_free_device_list(device_list);
    device_list = nullptr;

    // query port properties
    if (ibv_query_port(ctx, port, &port_attr)) {
        LOG(ERROR) << "ibv_query_port failed";
        goto CreateResourcesError;
    }

    // allocate Protection Domain
    pd = ibv_alloc_pd(ctx);
    if (!pd) {
        LOG(ERROR) << "ibv_alloc_pd failed";
        goto CreateResourcesError;
    }

    if (ibv_query_gid(ctx, port, gid_index, &this->gid)) {
        LOG(INFO) << "Max gid: " << port_attr.gid_tbl_len;
        LOG(ERROR) << "could not get gid for port: " << port << ", gid_index: " << gid_index;
        goto CreateResourcesError;
    }

    // Success :)
    this->dev_index = dev_index;
    this->gid_index = gid_index;
    this->port = port;
    this->ctx = ctx;
    this->pd = pd;
    this->lid = port_attr.lid;

    // check device memory support
    checkDMSupported();
    return;

CreateResourcesError:
    LOG(ERROR) << "Error Encountered, Cleanup ...";

    if (pd) ibv_dealloc_pd(pd);
    if (ctx) ibv_close_device(ctx);
    if (device_list) ibv_free_device_list(device_list);
}

void Context::checkDMSupported() {
    ibv_query_device_ex_input input;
    ibv_device_attr_ex attrs;
    memset(&input, 0, sizeof(input));
    memset(&attrs, 0, sizeof(attrs));
    if (ibv_query_device_ex(ctx, &input, &attrs)) {
        LOG(ERROR) << "Couldn't query device attributes";
    }
    device_memory_size = attrs.max_dm_size;
    LOG(INFO) << "NIC Device Memory is " << device_memory_size / 1024 << "KB";
}

Context::~Context() {
#ifndef NO_DESTRUCT
    if (pd && ibv_dealloc_pd(pd)) {
        LOG(ERROR) << "ibv_dealloc_pd failed: " << strerror(errno);
    }
    if (ctx && ibv_close_device(ctx)) {
        LOG(ERROR) << "ibv_close_device failed";
    }
#endif
}

ibv_mr *Context::createMR(void *addr, uint64_t size, bool on_chip, bool odp, bool mw_binding) {
    // If not on_chip, addr should be a pre-allocated buffer.
    // odp and mw_binding are suitable for not on-chip memory.
    if (!on_chip) {
        int flag = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC;
        if (odp) flag |= IBV_ACCESS_ON_DEMAND;
        if (mw_binding) flag |= IBV_ACCESS_MW_BIND;
        ibv_mr *mr = ibv_reg_mr(pd, (void *)addr, size, flag);

        if (!mr) LOG(ERROR) << "Memory registration failed";

        return mr;
    } else {
        return createMROnChip(addr, size);
    }
}

ibv_mr *Context::createMROnChip(void *addr, uint64_t size) {
    std::ignore = addr;
    ibv_alloc_dm_attr dm_attr;
    memset(&dm_attr, 0, sizeof(ibv_alloc_dm_attr));
    dm_attr.length = size;
    ibv_dm *dm = ibv_alloc_dm(ctx, &dm_attr);
    rt_assert_ptr(dm, "ibv_alloc_dm failed");

    ibv_mr *mr = ibv_reg_dm_mr(
        pd, dm, 0, size,
        IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC);

    rt_assert_ptr(mr, "ibv_reg_dm_mr failed");

    char *buf = (char *)malloc(size);
    memset(buf, 0, size);
    ibv_memcpy_to_dm(dm, 0, buf, 0);
    free(buf);
    return mr;
}

ibv_cq *Context::createCQ(int cqe, void *cq_ctx, ibv_comp_channel *channel) {
    // if ctx != nullptr, then it will be in ret->context
    // if channel != nullptr, then event-based things will be used.
    // create a full-functional ex cq.
    // including completion timestamp etc.
    ibv_cq_init_attr_ex attr;
    memset(&attr, 0, sizeof(attr));
    attr.cqe = cqe;
    attr.cq_context = cq_ctx;
    attr.channel = channel;
    attr.comp_vector = 0;
    attr.wc_flags = IBV_CREATE_CQ_SUP_WC_FLAGS;
    ibv_cq *ret = ibv_cq_ex_to_cq(ibv_create_cq_ex(ctx, &attr));
    rt_assert_ptr(ret, "Create CQ error");
    return ret;
}

ibv_srq *Context::createSRQ(int queue_depth, int sgl_size) {
    // shared receive queue, can be used by multiple QPs.
    ibv_srq_init_attr_ex attr;
    memset(&attr, 0, sizeof(attr));
    attr.attr.max_wr = queue_depth;
    attr.attr.max_sge = sgl_size;
    attr.pd = pd;
    // TODO: XRC domain?
    ibv_srq *ret = ibv_create_srq_ex(ctx, &attr);
    rt_assert_ptr(ret, "Create SRQ error");
    return ret;
}

QP Context::createQP(ibv_qp_type mode, ibv_cq *send_cq, ibv_cq *recv_cq, ibv_srq *srq, int queue_depth, int sgl_size,
                     uint32_t max_inline_data) {
    ibv_qp_init_attr_ex attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_type = mode;
    attr.sq_sig_all = 0;
    attr.send_cq = send_cq;
    attr.recv_cq = recv_cq;
    attr.srq = srq;
    attr.pd = pd;
    attr.send_ops_flags = IBV_QP_EX_WITH_SEND;
    attr.comp_mask |= IBV_QP_INIT_ATTR_PD | IBV_QP_INIT_ATTR_SEND_OPS_FLAGS;

    attr.cap.max_send_wr = queue_depth;
    attr.cap.max_recv_wr = queue_depth;
    attr.cap.max_send_sge = sgl_size;
    attr.cap.max_recv_sge = sgl_size;
    attr.cap.max_inline_data = max_inline_data;
    ibv_qp *qp = ibv_create_qp_ex(ctx, &attr);
    if (qp == nullptr) {
        LOG(ERROR) << "Create QP error: " << strerror(errno);
    }
    QP ret = QP(qp, this);
    return ret;
}

QP Context::createQP(ibv_qp_type mode, ibv_cq *cq, ibv_srq *srq, int queue_depth, int sgl_size,
                     uint32_t max_inline_data) {
    return createQP(mode, cq, cq, srq, queue_depth, sgl_size, max_inline_data);
}

void Context::printDeviceInfoEx() {
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
};  // namespace rdma