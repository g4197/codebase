#ifndef BENCHMARK_H_
#define BENCHMARK_H_

#include <thread>

#include "defs.h"
#include "numa_utils.h"
#include "thread_utils.h"
#include "timer.h"

struct TotalOp {
    char pad_front[hardware_destructive_interference_size];
    uint64_t ops = 0;
    char pad_back[hardware_destructive_interference_size - sizeof(uint64_t)];
};

/*
 * A simple benchmark util.
 * total_op is an array of a thread's total ops, used to calculate tput,
 * update method: total_op[my_thread_id].ops++; // += k
 * param to pass: strategy, num_threads, total_op array and a lambda (or other function)
 * Action: this class will create num_threads threads, initialize my_thread_id and my_numa_id, then run.
 * By using printTputAndJoin, we can get the throughput.
 */
class Benchmark {
public:
    enum BindCoreStrategy { kNUMAUniform = -1, kNUMA0 = 0, kNUMA1 };
    template<class Function, class... Args>
    inline static Benchmark run(BindCoreStrategy strategy, uint64_t num_threads, TotalOp *total_op, Function &&f,
                                Args &&...args) {
        Benchmark benchmark;
        benchmark.strategy_ = strategy;
        benchmark.total_op_ = total_op;
        benchmark.num_threads_ = num_threads;
        benchmark.threads_ = new Thread[num_threads];

        if (strategy != kNUMAUniform) {
            for (size_t i = 0; i < num_threads; ++i) {
                benchmark.threads_[i] = Thread((int)strategy, f, args...);
            }
        } else {
            for (size_t i = 0; i < num_threads; ++i) {
                benchmark.threads_[i] = Thread(i % numa_nodes(), f, args...);
            }
        }
        return benchmark;
    }

    inline void printTputAndJoin(std::string name = "") {
        uint64_t numa = strategy_ == kNUMAUniform ? 0 : (strategy_ + 1) % numa_nodes();
        Thread *counter_thread = new Thread(numa, [&]() {
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
        for (size_t i = 0; i < num_threads_; ++i) {
            threads_[i].join();
        }
        counter_thread->join();
    }

private:
    uint64_t num_threads_;
    Thread *threads_;
    TotalOp *total_op_;
    BindCoreStrategy strategy_;
};

#endif  // BENCHMARK_H_