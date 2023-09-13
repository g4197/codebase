#include "extutils.h"

AllocatorInfo infos[kMaxNUMANodes];
void *cur_map_start_addr = kPMMapStartAddr;
void *numa_addr_start[kMaxNUMANodes];
void *numa_addr_end[kMaxNUMANodes];
__thread char thread_buf[kThreadBufSize] __attribute__((aligned(kPMLineSize)));
thread_local CoroScheduler coro_scheduler;
