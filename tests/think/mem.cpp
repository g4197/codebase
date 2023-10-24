#include <bits/stdc++.h>
#include <immintrin.h>
#include <utils/benchmark.h>
using namespace std;

constexpr uint64_t kMemSize = 32ull << 30;  // 32GB
constexpr int kThreads = 16;
constexpr int kRounds = 100;

TotalOp total_op[kThreads];
int main() {
    Benchmark::run(Benchmark::kNUMA0, kThreads, total_op, [&]() {
        void *ptr = numa_alloc_onnode(kMemSize, 0);
        __m512i sum = _mm512_setzero_si512();
        for (int i = 0; i < kRounds; ++i) {
            for (char *c = (char *)ptr; c < (char *)ptr + kMemSize; c += 256) {
                sum = _mm512_add_epi64(sum, _mm512_load_si512(c));
                sum = _mm512_add_epi64(sum, _mm512_load_si512(c + 64));
                sum = _mm512_add_epi64(sum, _mm512_load_si512(c + 128));
                sum = _mm512_add_epi64(sum, _mm512_load_si512(c + 192));
                total_op[my_thread_id].ops += 256;  // load 256B.
            }
        }
    }).printTputAndJoin();
}