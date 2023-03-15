#include <thread>

#include "stdrdma.h"
#include "stdutils.h"
#include "unistd.h"

using namespace rdma;

// Code to understand the performance of cache coherence.
// The server core will fetch the "sent" + "finished" cacheline from the client cores' cache.
/*
Xeon Platinum 8360Y (Turbo Boost disabled):
    Local Socket L2->L2 HIT  latency        58.9
    Local Socket L2->L2 HITM latency        59.8
    Remote Socket L2->L2 HITM latency (data address homed in writer socket)
                            Reader Numa Node
    Writer Numa Node     0       1
                0        -   126.0
                1    126.1       -
    Remote Socket L2->L2 HITM latency (data address homed in reader socket)
                            Reader Numa Node
    Writer Numa Node     0       1
                0        -   128.8
                1    128.4       -
*/
// When kThreads = 4, tput is bounded by sent -> server, which is ~60ns (tput: 15.65M).
// Turbo boost can cause the multi-thread performance unstable...
struct alignas(kCacheLineSize) Slot {
    atomic<bool> sent;
    atomic<bool> finished;
    char pad2[hardware_destructive_interference_size - 2 * sizeof(atomic<bool>)];
};

constexpr int kThreads = 4;
int main(int argc, char **argv) {
    TotalOp total_op[kThreads];
    Slot slot[kThreads];
    auto bm = Benchmark::run(Benchmark::kNUMA0, kThreads, total_op, [&]() {
        while (true) {
            total_op[my_thread_id].ops++;
            slot[my_thread_id].sent.store(true, std::memory_order_release);
            while (slot[my_thread_id].finished.load(std::memory_order_acquire) == false) {}
            slot[my_thread_id].finished.store(false, std::memory_order_release);
        }
    });
    Thread t(0, [&]() {
        while (true) {
            for (int i = 0; i < kThreads; ++i) {
                while (slot[i].sent.load(std::memory_order_acquire) == false) {}
                slot[i].sent.store(false, std::memory_order_release);
                slot[i].finished.store(true, std::memory_order_release);
            }
        }
    });
    bm.printTputAndJoin();
}