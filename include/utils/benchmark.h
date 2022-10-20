#ifndef BENCHMARK_H_
#define BENCHMARK_H_

#include <thread>

#include "utils/defs.h"
#include "utils/numa_utils.h"
#include "utils/timer.h"

struct TotalOp {
    char pad_front[hardware_destructive_interference_size];
    uint64_t ops = 0;
    char pad_back[hardware_destructive_interference_size - sizeof(uint64_t)];
};

class Benchmark {
    /*
     * A simple benchmark util.
     * total_op is an array of a thread's total ops, used to calculate tput,
     * update method: total_op[my_thread_id].ops++; // += k
     * param to pass: strategy, num_threads, total_op array and a lambda (or other function)
     * Action: this class will create num_threads threads, initialize my_thread_id and my_numa_id, then run.
     * By using printTputAndJoin, we can get the throughput.
     */
public:
    enum BindCoreStrategy { kNUMAUniform = -1, kNUMA0 = 0, kNUMA1 };
    template<class Function, class... Args>
    static Benchmark run(BindCoreStrategy strategy, uint64_t num_threads, TotalOp *total_op, Function &&f,
                         Args &&...args) {
        Benchmark benchmark;
        benchmark.strategy_ = strategy;
        benchmark.total_op_ = total_op;
        benchmark.num_threads_ = num_threads;
        benchmark.threads_ = new std::thread[num_threads];
        for (size_t i = 0; i < num_threads; ++i) {
            benchmark.threads_[i] = std::thread([&] {
                my_thread_id = i;
                my_numa_id = strategy == kNUMAUniform ? i % kNUMANodes : (int)strategy;
                f(args...);
            });
        }
        if (strategy != kNUMAUniform) {
            for (size_t i = 0; i < num_threads; ++i) {
                bind_to_core(benchmark.threads_[i].native_handle(), 0, i);
            }
        } else {
            for (size_t i = 0; i < num_threads; ++i) {
                bind_to_core(benchmark.threads_[i].native_handle(), i % kNUMANodes, i / kNUMANodes);
            }
        }
        return benchmark;
    }

    void printTputAndJoin(std::string name = "") {
        std::thread *counter_thread = new std::thread([&]() {
            uint64_t cur = 0, prev = 0;
            while (true) {
                cur = 0;
                for (size_t i = 0; i < num_threads_; ++i) {
                    cur += total_op_[i].ops;
                }
                if (cur - prev < 1e3) {
                    LOG(INFO) << name << " Throughput: " << (cur - prev) << " ops/s";
                } else if (cur - prev < 1e6) {
                    LOG(INFO) << name << " Throughput: " << (cur - prev) / 1e3 << " Kops/s";
                } else if (cur - prev < 1e9) {
                    LOG(INFO) << name << " Throughput: " << (cur - prev) / 1e6 << " Mops/s";
                } else {
                    LOG(INFO) << name << " Throughput: " << (cur - prev) / 1e9 << " Gops/s";
                }

                prev = cur;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        });
        uint64_t numa = strategy_ == kNUMAUniform ? 0 : (strategy_ + 1) % kNUMANodes;
        uint64_t core = strategy_ == kNUMAUniform ? num_threads_ / kNUMANodes + 1 : 0;
        bind_to_core(counter_thread->native_handle(), numa, core);
        for (size_t i = 0; i < num_threads_; ++i) {
            threads_[i].join();
        }
    }

private:
    uint64_t num_threads_;
    std::thread *threads_;
    TotalOp *total_op_;
    BindCoreStrategy strategy_;
};

#endif  // BENCHMARK_H_