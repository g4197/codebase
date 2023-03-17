#include "stdutils.h"

__thread int my_thread_id = kInvalidThreadNUMAId;
__thread int my_numa_id = kInvalidThreadNUMAId;

std::atomic<int> Thread::tot_threads(0);
std::atomic<int> Thread::numa_tot_threads[kMaxNUMANodes] = {};
