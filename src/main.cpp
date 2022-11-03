#include <atomic>
#include <iostream>

#include "stdrdma.h"
#include "stdutils.h"

int main() {
    Thread t[2];
    for (int i = 0; i < 2; ++i) {
        t[i] = Thread(i, []() {
            rdma::Context ctx(rdma::numa_aware_nic_port(my_numa_id));
            char buffer[1024];
            ibv_mr *mr = ctx.createMR(buffer, 1024);
            ibv_cq *cq = ctx.createCQ();
            rdma::QP qp = ctx.createQP(ibv_qp_type::IBV_QPT_RC, cq);
            qp.publish("qp" + my_thread_id);
            qp.modifyToRTR("qp" + (1 - my_thread_id));
            qp.modifyToRTS();
        });
    }
    for (int i = 0; i < 2; ++i) {
        t[i].join();
        // delete t[i];
    }
}