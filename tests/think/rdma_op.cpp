#include <bits/stdc++.h>
#include <signal.h>

#include "gperftools/profiler.h"
#include "stdrdma.h"
#include "stdutils.h"

using namespace std;
using namespace rdma;

constexpr int kThreads = 8;
constexpr char kServerIP[] = "10.0.2.120";
constexpr char kClientIP[] = "10.0.2.120";

/*
 * 2 CX6 NICs, NUMA 0 -> NUMA 1.
 * device memory
 * CAS (conflict) 8B: 8.65Mops/s
 * FAA (conflict) 8B: 8.27Mops/s
 * Write 64B (no conflict): 93.8Mops/s
 * Read 64B (no conflict): 121.0Mops/s
 * host memory
 * CAS (conflict) 8B: 2.04Mops/s
 * FAA (conflict) 8B: 2.17Mops/s
 * Write 64B (no conflict): 106.3Mops/s
 * Read 64B (no conflict): 65.0Mops/s
 * Write 64B (conflict): 111.9Mops/s
 * Read 64B (conflict): 4.6Mops/s ?????
 */

TotalOp total_op[kThreads];
int main() {
    atomic<int> barrier(0);
    Benchmark bm = Benchmark::run(Benchmark::kNUMA0, kThreads, total_op, [&]() {
        LOG(INFO) << "My thread id is " << my_thread_id;
        rdma::Context ctx(kClientIP, 10001 + my_thread_id, 1, 3);
        LOG(INFO) << "Context " << &ctx << " port " << 10001 + my_thread_id;
        ibv_cq *cq = ctx.createCQ();
        char *send_buf = new char[131072];
        ibv_mr *mr = ctx.createMR(send_buf, 131072);
        QP qp = ctx.createQP(IBV_QPT_RC, cq);
        sleep(1);
        if (!qp.connect(kServerIP, 20000, my_thread_id)) {}
        qp.printState();
        sleep(2);
        MRInfo chip_mr_info = ctx.getMRInfo(kServerIP, 20000, 0);
        LOG(INFO) << chip_mr_info.addr << " " << chip_mr_info.valid;
        sleep(2);
        // MRInfo chip_mr_info = ctx.getMRInfo(kServerIP, 20000, kMROnChipIdStart);
        uint64_t cur = 0;
        while (true) {
            for (int i = 0; i < 1; ++i) {
                qp.read((uint64_t)mr->addr, chip_mr_info.addr, 16, mr->lkey, chip_mr_info.rkey, IBV_SEND_SIGNALED);
                // qp.write((uint64_t)mr->addr, chip_mr_info.addr, 64, mr->lkey, chip_mr_info.rkey, IBV_SEND_SIGNALED);
                // qp.read((uint64_t)mr->addr, chip_mr_info.addr + my_thread_id * 64, 64, mr->lkey, chip_mr_info.rkey,
                //         IBV_SEND_SIGNALED);
                // qp.write((uint64_t)mr->addr, chip_mr_info.addr + my_thread_id * 64, 64, mr->lkey, chip_mr_info.rkey,
                //          IBV_SEND_SIGNALED);
                // qp.cas((uint64_t)mr->addr, chip_mr_info.addr, cur, !cur, mr->lkey, chip_mr_info.rkey,
                //        IBV_SEND_SIGNALED);
                // cur = !cur;
                // qp.faa((uint64_t)mr->addr, chip_mr_info.addr, 1, mr->lkey, chip_mr_info.rkey);
            }
            ibv_wc wc[1];
            qp.pollSendCQ(1, wc);
            qp.printState();
            exit(0);
            total_op[my_thread_id].ops += 1;
        }
    });
    Thread t(1, [&]() {
        LOG(INFO) << "Thread start " << my_thread_id;
        rdma::Context ctx(kServerIP, 20000, 0, 3);

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
            LOG(INFO) << "Server conn";
            if (!qp[i].connect(kClientIP, 10001 + i, 0)) {
                LOG(INFO) << "Connect failed";
            }
            LOG(INFO) << "Server conn finished";
            qp[i].printState();
        }
        LOG(INFO) << "Connect all finished";
        while (true) {
            sleep(1);
        }
    });
    bm.printTputAndJoin();
    while (true) {
        sleep(1);
    }
}