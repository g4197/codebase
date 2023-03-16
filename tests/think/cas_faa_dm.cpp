#include <bits/stdc++.h>
#include <signal.h>

#include "gperftools/profiler.h"
#include "stdrdma.h"
#include "stdutils.h"

using namespace std;
using namespace rdma;

void gprofStartAndStop(int signum) {
    static int isStarted = 0;
    if (signum != SIGUSR1) return;
    if (!isStarted) {
        isStarted = 1;
        ProfilerStart("prof/dm.prof");
        fprintf(stderr, "ProfilerStart success\n");
    } else {
        isStarted = 0;
        ProfilerStop();
        fprintf(stderr, "ProfilerStop success\n");
    }
}

constexpr int kThreads = 32;
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

TotalOp total_op[kThreads];
int main() {
    signal(SIGUSR1, gprofStartAndStop);
    atomic<int> barrier(0);
    Benchmark bm = Benchmark::run(Benchmark::kNUMA0, kThreads, total_op, [&]() {
        rdma::Context ctx(kClientIP, 10000 + my_thread_id, 0);
        ibv_cq *cq = ctx.createCQ();
        char *send_buf = new char[131072];
        ibv_mr *mr = ctx.createMR(send_buf, 131072);
        QP qp = ctx.createQP(IBV_QPT_RC, cq);
        sleep(1);
        if (!qp.connect(kServerIP, 20000, my_thread_id)) {}
        sleep(2);
        MRInfo *chip_mr_info = new MRInfo;
        *chip_mr_info = ctx.getMRInfo(kServerIP, 20000, kMRIdOnChipStart);

        uint64_t cur = 0;
        while (true) {
            for (int i = 0; i < 32; ++i) {
                // Read is sequenced?
                qp.read((uint64_t)mr->addr + i * 64, chip_mr_info->addr, 4, mr->lkey, chip_mr_info->rkey,
                        IBV_SEND_SIGNALED);
                // qp.cas((uint64_t)mr->addr, chip_mr_info.addr, cur, !cur, mr->lkey, chip_mr_info.rkey,
                //        IBV_SEND_SIGNALED);
                // cur = !cur;
            }
            ibv_wc wc[32];
            qp.pollSendCQ(32, wc);
            total_op[my_thread_id].ops += 32;
        }
    });
    Thread t(1, [&]() {
        LOG(INFO) << "Thread start " << my_thread_id;
        rdma::Context ctx(kServerIP, 20000, 1);

        char *p = (char *)numa_alloc_onnode(131072, 1);
        ibv_mr *mr = ctx.createMR(p, 131072);
        ibv_mr *chip_mr = ctx.createMROnChip(131072);
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
        sleep(3);
        while (true) {
            sleep(1);
        }
    });
    bm.printTputAndJoin();
    while (true) {
        sleep(1);
    }
}