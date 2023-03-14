#include <thread>

#include "stdrdma.h"
#include "stdutils.h"
#include "unistd.h"

using namespace rdma;

constexpr int kThreads = 1;
int main(int argc, char **argv) {
    TotalOp total_op[kThreads];
    atomic<bool> finished;
    atomic<bool> sent;
    auto bm = Benchmark::run(Benchmark::kNUMA0, kThreads, total_op, [&]() {
        while (true) {
            total_op[my_thread_id].ops++;
            while (sent.load(std::memory_order_acquire) == false) {}
            sent.store(false, std::memory_order_release);
            finished.store(true, std::memory_order_release);
        }
    });
    Thread t(0, [&]() {
        while (true) {
            sent.store(true, std::memory_order_release);
            while (finished.load(std::memory_order_acquire) == false) {}
            finished.store(false, std::memory_order_release);
        }
    });
    bm.printTputAndJoin();
}