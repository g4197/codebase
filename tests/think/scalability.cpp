#include <bits/stdc++.h>

#include "stdrdma.h"
#include "stdutils.h"

using namespace rdma;
using namespace std;

// Understand the scalability of RC QPs.
// Usage: ./scalability [threads] [qps]
// Environment: 2 * Xeon Platinum 8360Y, 2 * ConnectX-6 NICs
// 32 threads, 32 QPs: write 123Mops/s, read 83Mops/s
// 32 threads, 64 QPs: write 121Mops/s, read 55Mops/s
// 32 threads, 512 QPs: write 122Mops/s, read 42Mops/s
// 32 threads, 4096 QPs: write 111Mops/s, read 21Mops/s
// 32 threads, 16384 QPs: write 105Mops/s, read ?Mops/s (error...)

// read or write
#define qp_op read

constexpr char kClientIP[] = "10.0.2.175";
constexpr char kServerIP[] = "10.0.2.175";
int kThreads = 0;
int kQPNum = 0;
int kQPPerThread = 0;
TotalOp total_op[512];

void barrier() {
    static atomic<int> barrier(0);
    ++barrier;
    while (barrier != kThreads * 2) {
        this_thread::yield();
    }
}

void barrier2() {
    static atomic<int> barrier(0);
    ++barrier;
    while (barrier != kThreads * 2) {
        this_thread::yield();
    }
}

int main(int argc, char **argv) {
    if (argc > 1) {
        kThreads = atoi(argv[1]);
        kQPNum = atoi(argv[2]);
        LOG(INFO) << "Using " << kThreads << " threads, " << kQPNum << " QPs";
        kQPPerThread = kQPNum / kThreads;
    }
    Benchmark bm = Benchmark::run(Benchmark::kNUMA0, kThreads, total_op, [&]() {
        rdma::Context ctx(kClientIP, 10001 + my_thread_id, 0);
        ibv_mr *mr = ctx.createMR(new char[131072], 131072);
        QP qp[kQPPerThread];
        for (int i = 0; i < kQPPerThread; ++i) {
            ibv_cq *cq = ctx.createCQ();
            qp[i] = ctx.createQP(IBV_QPT_RC, cq);
        }
        barrier();
        for (int i = 0; i < kQPPerThread; ++i) {
            if (!qp[i].connect(kServerIP, 20000 + my_thread_id, i)) {
                LOG(INFO) << "Connect failed";
            }
        }
        barrier2();
        MRInfo mr_info = ctx.getMRInfo(kServerIP, 20000 + my_thread_id, 0);
        for (int i = 0; i < kQPPerThread; ++i) {
            for (int j = 0; j < 31; ++j) {
                qp[i].qp_op((uint64_t)mr->addr + i * kCacheLineSize, mr_info.addr + i * kCacheLineSize, kCacheLineSize,
                            mr->lkey, mr_info.rkey);
            }
            qp[i].qp_op((uint64_t)mr->addr + 32 * kCacheLineSize, mr_info.addr + 32 * kCacheLineSize, kCacheLineSize,
                        mr->lkey, mr_info.rkey, IBV_SEND_SIGNALED);
        }
        while (true) {
            for (int i = 0; i < kQPPerThread; ++i) {
                for (int j = 0; j < 31; ++j) {
                    qp[i].qp_op((uint64_t)mr->addr + i * kCacheLineSize, mr_info.addr + i * kCacheLineSize,
                                kCacheLineSize, mr->lkey, mr_info.rkey);
                }
                qp[i].qp_op((uint64_t)mr->addr + 32 * kCacheLineSize, mr_info.addr + 32 * kCacheLineSize,
                            kCacheLineSize, mr->lkey, mr_info.rkey, IBV_SEND_SIGNALED);
            }
            for (int i = 0; i < kQPPerThread; ++i) {
                ibv_wc wc;
                qp[i].pollSendCQ(1, &wc);
                total_op[my_thread_id].ops += 32;
            }
        }
    });

    Thread t[kThreads];
    for (int i = 0; i < kThreads; ++i) {
        t[i] = Thread(1, [&]() {
            rdma::Context ctx(kServerIP, 20000 + my_thread_id - kThreads, 1);
            ibv_mr *mr = ctx.createMR(new char[131072], 131072);
            QP qp[kQPPerThread];
            for (int i = 0; i < kQPPerThread; ++i) {
                ibv_cq *cq = ctx.createCQ();
                qp[i] = ctx.createQP(IBV_QPT_RC, cq);
            }
            barrier();
            for (int i = 0; i < kQPPerThread; ++i) {
                if (!qp[i].connect(kClientIP, 10001 + my_thread_id - kThreads, i)) {
                    LOG(INFO) << "Connect failed";
                }
            }
            barrier2();
            while (true) {
                sleep(1);
            }
        });
    }

    bm.printTputAndJoin();
}