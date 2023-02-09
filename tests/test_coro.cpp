#include "stdutils.h"

int main() {
    TotalOp total_op[128];
    // Xeon Gold 5220, -O3, -g, ~28Mops/s per thread.
    Benchmark::run(Benchmark::kNUMA0, 1, total_op, [&]() {
        for (int i = 0; i < kMaxCoroutinesPerThread; ++i) {
            coro_scheduler.insert([]() {
                while (true) {
                    coro_yield();
                }
            });
        }
        while (true) {
            coro_scheduler.next();
            total_op[my_thread_id].ops++;
        }
    }).printTputAndJoin();
}