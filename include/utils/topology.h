#ifndef TOPOLOGY_H_
#define TOPOLOGY_H_

constexpr int kNUMANodes = 2;
constexpr int kCoresPerNUMANode = 36;
constexpr int kThreadsPerNUMANode = kCoresPerNUMANode * 2;
constexpr int kMaxThreads = 256;
extern __thread int my_thread_id;
extern __thread int my_numa_id;

#endif  // TOPOLOGY_H_