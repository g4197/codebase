#include <atomic>
#include <iostream>

#include "utils/benchmark.h"

std::atomic<uint64_t> counter;
constexpr uint64_t kThreads = 64;

TotalOp total_op[kThreads];

int main() {
    auto benchmark = Benchmark::run(Benchmark::kNUMAUniform, kThreads, total_op, [&]() {
        while (true) {
            counter.fetch_add(1, std::memory_order_relaxed);
            total_op[my_thread_id].ops++;
        }
    });
    benchmark.printTputAndJoin("Fetch-And-Add test");
}