#include <bits/stdc++.h>

#include "stdutils.h"

using namespace std;

constexpr int kMaxThreads = 28;
TotalOp total_op[kMaxThreads];
VersionedRWLock rwlock;
struct alignas(16) CMTime {
    uint64_t ctime;
    uint64_t mtime;
} cmtime;

int main() {
    Benchmark::run(Benchmark::kNUMA0, kMaxThreads, total_op, [&]() {
        while (true) {
            rwlock.lock_shared();
            uint64_t tm = time(nullptr);
            do {
                CMTime old = cmtime;
                CMTime cur{ tm, tm };
                if (old.ctime >= tm ||
                    __sync_bool_compare_and_swap((__int128_t *)&cmtime, *(__int128_t *)&old, *(__int128_t *)&cur)) {
                    break;
                }
            } while (true);
            rwlock.unlock_shared();

            // spin for 2us.
            auto now = std::chrono::high_resolution_clock::now();
            while (true) {
                auto cnow = std::chrono::high_resolution_clock::now();
                if (cnow - now > std::chrono::microseconds(2)) break;
            }
            total_op[my_thread_id].ops++;
        }
    }).printTputAndJoin();
}