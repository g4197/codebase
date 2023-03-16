#include <bits/stdc++.h>

#include "stdrdma.h"
#include "stdutils.h"

using namespace std;
using namespace rdma;

constexpr int kThreads = 36;
constexpr char kServerIP[] = "10.0.2.175";
constexpr char kClientIP[] = "10.0.2.175";

inline void fillSgeWr(ibv_sge &sg, ibv_send_wr &wr, uint64_t source, uint64_t size, uint32_t lkey) {
    memset(&sg, 0, sizeof(sg));
    sg.addr = (uintptr_t)source;
    sg.length = size;
    sg.lkey = lkey;

    memset(&wr, 0, sizeof(wr));
    wr.wr_id = 0;
    wr.sg_list = &sg;
    wr.num_sge = 1;
}

int main() {
    atomic<int> barrier(0);
    TotalOp total_op[kThreads];
    Benchmark bm = Benchmark::run(Benchmark::kNUMA0, kThreads, total_op, [&]() {
        rdma::Context ctx(kClientIP, 10000 + my_thread_id, 0);
        ibv_cq *cq = ctx.createCQ();
        char *send_buf = new char[1024];
        ibv_mr *mr = ctx.createMR(send_buf, 1024);
        QP qp = ctx.createQP(IBV_QPT_RC, cq);
        sleep(1);
        if (!qp.connect(kServerIP, 20000, my_thread_id)) {}
        ++barrier;
        while (barrier != kThreads + 1) {}
        MRInfo chip_mr_info = ctx.getMRInfo(kServerIP, 20000, 0);
        uint64_t cur = 0;
        ibv_sge sg;
        ibv_send_wr wr;
        ibv_send_wr *bad_wr;
        auto source = (uint64_t)(send_buf + 8 * my_thread_id);
        auto dest = chip_mr_info.addr;
        auto size = 8;
        auto lkey = mr->lkey;
        auto rkey = chip_mr_info.rkey;
        fillSgeWr(sg, wr, source, size, lkey);
        wr.wr.atomic.remote_addr = dest;
        wr.wr.atomic.rkey = rkey;

        wr.opcode = IBV_WR_ATOMIC_CMP_AND_SWP;
        wr.send_flags = IBV_SEND_SIGNALED;
        wr.wr_id = 0;
        while (true) {
            for (int i = 0; i < 32; ++i) {
                wr.wr.atomic.compare_add = cur;
                wr.wr.atomic.swap = !cur;
                ibv_post_send(qp.qp, &wr, &bad_wr);
            }
            ibv_wc wc[32];
            qp.pollSendCQ(32, wc);
            total_op[my_thread_id].ops += 32;
        }
    });
    Thread t(1, [&]() {
        rdma::Context ctx(kServerIP, 20000, 1);
        ibv_mr *mr = ctx.createMR(new char[1024], 1024);
        ibv_mr *chip_mr = ctx.createMROnChip(1024);
        ibv_cq *cq = ctx.createCQ();
        QP qp[kThreads];
        for (int i = 0; i < kThreads; ++i) {
            qp[i] = ctx.createQP(IBV_QPT_RC, cq);
        }
        sleep(1);
        for (int i = 0; i < kThreads; ++i) {
            if (!qp[i].connect(kClientIP, 10000 + i, 0)) {
                LOG(INFO) << "Connect failed";
            }
        }
        ++barrier;
        while (barrier != kThreads + 1) {}
        while (true) {
            sleep(1);
        }
    });
    bm.printTputAndJoin();
}