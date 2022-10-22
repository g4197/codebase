#include "utils/pm_utils.h"

AllocatorInfo infos[kMaxNUMANodes];
void *cur_map_start_addr = kPMMapStartAddr;
void *numa_addr_start[kMaxNUMANodes];
void *numa_addr_end[kMaxNUMANodes];
__thread char thread_buf[kThreadBufSize] __attribute__((aligned(kPMLineSize)));
__thread int my_thread_id;
__thread int my_numa_id;