#ifndef NUMA_UTILS_H_
#define NUMA_UTILS_H_
// Borrowed from eRPC.

#include <numa.h>
#include <stdio.h>

#include <thread>
#include <vector>

#include "utils/defs.h"
#include "utils/log.h"

inline void rt_assert(bool condition, std::string throw_str) {
    if (unlikely(!condition)) {
        LOG(ERROR) << "Assertion failed: " << throw_str;
    }
}

inline int numa_nodes() {
    // O(1) here. Assume numa_nodes won't change when running.
    // avoid repeatedly call numa_num_configured_nodes()
    static int ret = numa_num_configured_nodes();
    return ret;
}

inline size_t threads_per_numa_node() {
    static size_t ret = static_cast<size_t>(numa_num_configured_cpus() / numa_num_configured_nodes());
    return ret;
}

inline std::vector<size_t> get_lcores_for_numa_node(size_t numa_node) {
    rt_assert(numa_node <= static_cast<size_t>(numa_max_node()), "Invalid numa_node");

    std::vector<size_t> ret;
    size_t num_lcores = static_cast<size_t>(numa_num_configured_cpus());

    for (size_t i = 0; i < num_lcores; i++) {
        if (numa_node == static_cast<size_t>(numa_node_of_cpu(i))) {
            ret.push_back(i);
        }
    }
    return ret;
}

inline void bind_to_core(pthread_t native_handle, size_t numa_node, size_t numa_local_index) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    const std::vector<size_t> lcore_vec = get_lcores_for_numa_node(numa_node);
    if (numa_local_index >= lcore_vec.size()) {
        LOG(ERROR) << "Requested binding to core " << numa_local_index << " (zero-indexed) on NUMA node " << numa_node
                   << ", which has only " << lcore_vec.size() << " cores.";
        return;
    }

    const size_t global_index = lcore_vec.at(numa_local_index);

    CPU_SET(global_index, &cpuset);
    int rc = pthread_setaffinity_np(native_handle, sizeof(cpu_set_t), &cpuset);
    rt_assert(rc == 0, "Error setting thread affinity");
}

inline void bind_to_core(size_t numa_node, size_t numa_local_index) {
    return bind_to_core(pthread_self(), numa_node, numa_local_index);
}

inline void clear_affinity_for_process() {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    const size_t num_cpus = std::thread::hardware_concurrency();
    for (size_t i = 0; i < num_cpus; i++) CPU_SET(i, &mask);

    int ret = sched_setaffinity(0 /* whole-process */, sizeof(cpu_set_t), &mask);
    rt_assert(ret == 0, "Failed to clear CPU affinity for this process");
}

#endif  // NUMA_UTILS_H_