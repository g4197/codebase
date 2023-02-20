#include "stdrdma.h"
#include "stdutils.h"

int main() {
    Thread t0(0, []() {
        rdma::Context ctx("10.0.2.163", 31850, 0, 0);
        ibv_mr *send_mr = ctx.createMR(nullptr, 4, true, false, false);
        char *recv_buf = new char[1024];
        ibv_mr *recv_mr = ctx.createMR(recv_buf, 1024);
        ibv_cq *cq = ctx.createCQ();
        rdma::QP qp = ctx.createQP(IBV_QPT_RC, cq);
        LOG(INFO) << "Finish creating QP";
        sleep(5);
        qp.connect("10.0.2.163", 31851, 0);
        qp.recv((uint64_t)recv_mr->addr, recv_mr->length, recv_mr->lkey);
        ibv_wc wc;
        qp.pollRecvCQ(1, &wc);
        LOG(INFO) << recv_buf;
    });

    Thread t1(1, []() {
        rdma::Context ctx("10.0.2.163", 31851, 1, 0);
        char *send_buf = new char[1024];
        ibv_mr *send_mr = ctx.createMR(send_buf, 1024);
        memcpy(send_buf, "Hello World!", 13);
        ibv_mr *recv_mr = ctx.createMR(nullptr, 4, true, false, false);
        ibv_cq *cq = ctx.createCQ();
        rdma::QP qp = ctx.createQP(IBV_QPT_RC, cq);
        LOG(INFO) << "Finish creating QP";
        sleep(5);
        qp.connect("10.0.2.163", 31850, 0);
        qp.send((uint64_t)send_mr->addr, send_mr->length, send_mr->lkey);
        ibv_wc wc;
        qp.pollSendCQ(1, &wc);
    });
    t0.join();
    t1.join();
}