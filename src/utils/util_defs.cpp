#include "utils/coroutine.h"
#include "utils/pm_utils.h"
#include "utils/thread_utils.h"

AllocatorInfo infos[kMaxNUMANodes];
void *cur_map_start_addr = kPMMapStartAddr;
void *numa_addr_start[kMaxNUMANodes];
void *numa_addr_end[kMaxNUMANodes];
__thread char thread_buf[kThreadBufSize] __attribute__((aligned(kPMLineSize)));
__thread int my_thread_id = kInvalidThreadNUMAId;
__thread int my_numa_id = kInvalidThreadNUMAId;

std::atomic<int> Thread::tot_threads(0);
std::atomic<int> Thread::numa_tot_threads[kMaxNUMANodes] = {};

__thread CoroScheduler coro_scheduler;