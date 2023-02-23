#include <thread>

#include "stdrdma.h"
#include "unistd.h"

int main(int argc, char **argv) {
    if (argv[1][0] == '0') {
        LOG(INFO) << "0";
        rdma::Context ctx("10.0.2.163", 31850, 0, 0);
        char *recv_buf = new char[65536];
        ibv_mr *recv_mr = ctx.createMR(recv_buf, 65536);
        ibv_cq *cq = ctx.createCQ();
        rdma::QP qp = ctx.createQP(IBV_QPT_UD, cq);
        ctx.printDeviceInfoEx();
        LOG(INFO) << "Finish creating QP";
        sleep(5);
        qp.connect("10.0.2.163", 31851, 0);
        for (int i = 0; i < 16; ++i) {
            qp.recv((uint64_t)recv_mr->addr + i * 32, 128, recv_mr->lkey);
        }
        ibv_wc wc;
        qp.pollRecvCQ(1, &wc);
        LOG(INFO) << (recv_buf + 40);
    } else {
        LOG(INFO) << "1";
        rdma::Context ctx("10.0.2.163", 31851, 0, 0);
        char *send_buf = new char[1024];
        ibv_mr *send_mr = ctx.createMR(send_buf, 1024);
        memcpy(send_buf, "Hello World!", 13);
        ibv_cq *cq = ctx.createCQ();
        rdma::QP qp = ctx.createQP(IBV_QPT_UD, cq);
        LOG(INFO) << "Finish creating QP";
        sleep(5);
        qp.connect("10.0.2.163", 31850, 0);
        rdma::QPInfo info = ctx.getQPInfo("10.0.2.163", 31850, 0);
        if (!info.valid) {
            LOG(FATAL) << "QPInfo is not valid";
        }
        ibv_ah_attr attr;
        ctx.fillAhAttr(&attr, info);
        ibv_ah *ah = ibv_create_ah(ctx.pd, &attr);
        LOG(INFO) << "AH lid is " << attr.dlid;
        qp.send((uint64_t)send_mr->addr, 13, send_mr->lkey, ah, info.qpn, IBV_SEND_SIGNALED);

        ibv_wc wc;
        qp.pollSendCQ(1, &wc);
    }
}