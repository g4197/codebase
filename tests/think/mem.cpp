#include <bits/stdc++.h>
#include <immintrin.h>
#include <utils/benchmark.h>
using namespace std;

constexpr uint64_t kMemSize = 2ull << 30;
constexpr int kThreads = 16;
constexpr int kRounds = 100;

bool is_write = false;
TotalOp total_op[kThreads];
#pragma GCC push_options
#pragma GCC optimize("O0")
int main(int argc, char **argv) {
    if (argc >= 2) {
        is_write = stoi(argv[1]);
    }
    void *global = numa_alloc_onnode(kMemSize * kThreads, 1);
    Benchmark::run(Benchmark::kNUMA1, kThreads, total_op, [&]() {
        void *ptr = (char *)global + my_thread_id * kMemSize;
        uint32_t seed = my_thread_id + 42;
        __m512i sum1 = _mm512_set1_epi32(rand_r(&seed));
        __m512i sum2 = _mm512_set1_epi32(rand_r(&seed));
        __m512i sum3 = _mm512_set1_epi32(rand_r(&seed));
        __m512i sum4 = _mm512_set1_epi32(rand_r(&seed));
        if (!is_write) {
            for (char *c = (char *)ptr; c < (char *)ptr + kMemSize; c += 256) {
                _mm512_stream_si512((__m512i *)c, sum1);
                _mm512_stream_si512((__m512i *)(c + 64), sum2);
                _mm512_stream_si512((__m512i *)(c + 128), sum3);
                _mm512_stream_si512((__m512i *)(c + 192), sum4);
            }

            for (int i = 0; i < kRounds; ++i) {
                for (char *c = (char *)ptr; c < (char *)ptr + kMemSize; c += 256) {
                    sum1 = _mm512_add_epi64(_mm512_load_si512((__m512i *)(c)), _mm512_load_si512((__m512i *)(c + 64)));
                    sum2 = _mm512_add_epi64(_mm512_load_si512((__m512i *)(c + 128)),
                                            _mm512_load_si512((__m512i *)(c + 192)));
                    total_op[my_thread_id].ops += 256;  // load 256B.
                }
            }
        } else {
            for (int i = 0; i < kRounds; ++i) {
                for (char *c = (char *)ptr; c < (char *)ptr + kMemSize; c += 256) {
                    _mm512_stream_si512((__m512i *)c, sum1);
                    _mm512_stream_si512((__m512i *)(c + 64), sum2);
                    _mm512_stream_si512((__m512i *)(c + 128), sum3);
                    _mm512_stream_si512((__m512i *)(c + 192), sum4);
                    total_op[my_thread_id].ops += 256;  // store 256B.
                }
            }
        }
    }).printTputAndJoin();
}
#pragma GCC pop_options