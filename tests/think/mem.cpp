#include <bits/stdc++.h>
#include <immintrin.h>
#include <utils/benchmark.h>
using namespace std;

constexpr uint64_t kMemSize = 2ull << 30;
constexpr int kThreads = 28;
constexpr int kRounds = 100;

bool is_write = false;
TotalOp total_op[kThreads];
int main(int argc, char **argv) {
    if (argc >= 2) {
        is_write = stoi(argv[1]);
    }
    Benchmark::run(Benchmark::kNUMA0, kThreads, total_op, [&]() {
        void *ptr = numa_alloc_onnode(kMemSize, 0);
        __m512i sum = _mm512_setzero_si512();
        if (!is_write) {
            for (char *c = (char *)ptr; c < (char *)ptr + kMemSize; c += 256) {
                _mm512_stream_si512((__m512i *)c, sum);
                _mm512_stream_si512((__m512i *)(c + 64), sum);
                _mm512_stream_si512((__m512i *)(c + 128), sum);
                _mm512_stream_si512((__m512i *)(c + 192), sum);
            }

            for (int i = 0; i < kRounds; ++i) {
                for (char *c = (char *)ptr; c < (char *)ptr + kMemSize; c += 256) {
                    sum = _mm512_add_epi64(sum, _mm512_stream_load_si512(c));
                    sum = _mm512_add_epi64(sum, _mm512_stream_load_si512(c + 64));
                    sum = _mm512_add_epi64(sum, _mm512_stream_load_si512(c + 128));
                    sum = _mm512_add_epi64(sum, _mm512_stream_load_si512(c + 192));
                    total_op[my_thread_id].ops += 256;  // load 256B.
                }
            }
        } else {
            for (int i = 0; i < kRounds; ++i) {
                for (char *c = (char *)ptr; c < (char *)ptr + kMemSize; c += 256) {
                    _mm512_stream_si512((__m512i *)c, sum);
                    _mm512_stream_si512((__m512i *)(c + 64), sum);
                    _mm512_stream_si512((__m512i *)(c + 128), sum);
                    _mm512_stream_si512((__m512i *)(c + 192), sum);
                    total_op[my_thread_id].ops += 256;  // store 256B.
                }
            }
        }
    }).printTputAndJoin();
}