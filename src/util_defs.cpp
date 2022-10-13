#include "utils/pm_utils.h"
#include "utils/topology.h"

AllocatorInfo infos[kNUMANodes];
void *cur_map_start_addr = kPMMapStartAddr;
void *numa_addr_start[kNUMANodes];
void *numa_addr_end[kNUMANodes];
__thread char thread_buf[kThreadBufSize] __attribute__((aligned(kPMLineSize)));
__thread int my_thread_id;
__thread int my_numa_id;