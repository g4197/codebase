#include <atomic>
#include <iostream>

#include "stdrdma.h"
#include "stdutils.h"

struct A {
    int x;
    int y;
};

int main() {
    A a;
    int b;
    char s[5] = "abcd";
    Slice c(s, 4);
    char buf[200];
    uint64_t size = serialize(buf, a, b, c);
    Slice t, u;
    deserialize(buf, size, a, b, u);
    std::cerr << t.data() << " " << u.data() << std::endl;

    // Thread t[2];
    // for (int i = 0; i < 2; ++i) {
    //     t[i] = Thread(i, []() {
    //         rdma::Context ctx(rdma::numa_aware_nic_port(my_numa_id));
    //         char buffer[1024];
    //         ibv_mr *mr = ctx.createMR(buffer, 1024);
    //         ibv_cq *cq = ctx.createCQ();
    //         rdma::QP qp = ctx.createQP(ibv_qp_type::IBV_QPT_RC, cq);
    //         qp.publish("qp" + my_thread_id);
    //         qp.modifyToRTR("qp" + (1 - my_thread_id));
    //         qp.modifyToRTS();
    //     });
    // }
    // for (int i = 0; i < 2; ++i) {
    //     t[i].join();
    //     // delete t[i];
    // }
}