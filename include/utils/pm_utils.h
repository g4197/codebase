#ifndef PM_UTILS_H_
#define PM_UTILS_H_

#include <fcntl.h>
#include <libpmem.h>
#include <memkind.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cassert>

#include "glog/logging.h"
#include "utils/defs.h"

/*
 * A persistent memory allocator using memkind as volatile memory allocator.
 * Template parameter is NUMA node id (for zero-cost abstraction).
 * Should modify: kPMPrefix (/mnt/pm0 or /mnt/pm1 by default)
 */

extern void *cur_map_start_addr;
void *const kPMMapStartAddr = (void *)0x419700000000;
const std::string kPMPrefix = "/mnt/pm";
constexpr size_t kDRAMIdx = UINT64_MAX;
extern void *numa_addr_start[kNUMANodes];
extern void *numa_addr_end[kNUMANodes];

enum { kPoolCreate, kPoolOpen };
struct AllocatorInfo {
    std::string pool_name;
    memkind_t pmem_kind;
    void *start;
    size_t size;
};
extern AllocatorInfo infos[kNUMANodes];

inline void *dram_alloc(size_t size) {
    void *ptr;
    if (posix_memalign(&ptr, kCacheLineSize, size)) {
        LOG(ERROR) << "Failed to alloc memory";
        return nullptr;
    }
    return ptr;
}

inline void dram_free(void *ptr) {
    free(ptr);
}

template<size_t idx>
class GAllocator {
    static_assert(idx < kNUMANodes, "idx should be less than kNUMANodes");

public:
    static inline int init_pool(const std::string &pool_name, const size_t pool_size) {
#ifdef USE_DRAM
        return kPoolCreate;
#else
        AllocatorInfo *info = &infos[idx];
        int ret = kPoolCreate, fd = -1;

#ifndef DEVDAX
        std::string cur_pool_name = kPMPrefix + std::to_string(idx % kNUMANodes) + "/" + pool_name;
        info->start = cur_map_start_addr;
        cur_map_start_addr = (char *)cur_map_start_addr + pool_size;
#ifdef CREATE
        fd = open(cur_pool_name.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
        if (ftruncate(fd, pool_size) != 0) {
            LOG(ERROR) << "ftruncate failed";
            exit(0);
        }
        ret = kPoolCreate;
#else
        if (access(cur_pool_name.c_str(), F_OK) != 0) {
            fd = open(cur_pool_name.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
            if (ftruncate(fd, pool_size) != 0) {
                LOG(ERROR) << "ftruncate failed";
                exit(0);
            }
        } else {
            fd = open(cur_pool_name.c_str(), O_RDWR);
            struct stat st;
            stat(cur_pool_name.c_str(), &st);
            if (st.st_size != (int64_t)pool_size) {
                LOG(ERROR) << "size of " << cur_pool_name << " is not " << pool_size;
                exit(0);
            }
            ret = kPoolOpen;
        }
#endif  // create
#else
        char devdax_name_buf[1024];
        sprintf(devdax_name_buf, "/dev/dax%lu.0", idx % kNUMANodes);
        fd = open(devdax_name_buf, O_RDWR);
        ret = kPoolCreate;
        std::string cur_pool_name = devdax_name_buf;
#endif  // devdax

#ifdef PRE_FAULT
        info->start =
            mmap(info->start, pool_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED | MAP_POPULATE, fd, 0);
        LOG(INFO) << "mmap " << cur_pool_name << " " << info->start << " " << pool_size << " with prefault";
#else
        info->start = mmap(info->start, pool_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
        LOG(INFO) << "mmap " << cur_pool_name << " " << info->start << " " << pool_size << " without prefault";
#endif
        if (info->start == MAP_FAILED) {
            LOG(ERROR) << "mmap failed";
        }

        LOG(INFO) << "Map " << pool_name << " to " << info->start << " size " << pool_size / 1024 / 1024 << "MB";
        numa_addr_start[idx] = info->start;
        numa_addr_end[idx] = (char *)info->start + pool_size;
        // Don't use memkind_create_pmem for I want to keep the file.
        memkind_create_fixed(info->start, pool_size, &(info->pmem_kind));
        return ret;
#endif
    }

    static inline void *pm_alloc(size_t size) {
#ifdef USE_DRAM
        return dram_alloc(size);
#else
        void *ptr;
        for (int i = 0; i < kNUMANodes; i++) {
            AllocatorInfo *info = &infos[(idx + i) % kNUMANodes];
            if (likely(!memkind_posix_memalign(info->pmem_kind, &ptr, kCacheLineSize, size))) {
                return ptr;
            }
        }
        LOG(ERROR) << "Not enough memory";
        return nullptr;
#endif
    }
};

template<>
class GAllocator<kDRAMIdx> {
public:
    static inline int init_pool(const std::string &pool_name, const size_t pool_size) {
        return kPoolCreate;
    }
    static inline void *pm_alloc(size_t size) {
        return dram_alloc(size);
    }
};

inline void pm_free(void *ptr) {
#ifdef USE_DRAM
    return dram_free(ptr);
#else
    for (int i = 0; i < kNUMANodes; i++) {
        if (ptr >= numa_addr_start[i] && ptr < numa_addr_end[i]) {
            AllocatorInfo *info = &infos[i];
            memkind_free(info->pmem_kind, ptr);
            return;
        }
    }
    return dram_free(ptr);
#endif
}

template<size_t idx>
inline void *kvs_malloc(size_t size) {
    return GAllocator<idx>::pm_alloc(size);
}

template<>
inline void *kvs_malloc<kDRAMIdx>(size_t size) {
    return dram_alloc(size);
}

inline void clflush(char *data, int len) {
    volatile char *ptr = (char *)((unsigned long)data & ~(kCacheLineSize - 1));
    for (; ptr < data + len; ptr += kCacheLineSize) {
        asm volatile(".byte 0x66; xsaveopt %0" : "+m"(*(volatile char *)(ptr)));
    }
    sfence();
}

inline void *memcpy_nt(void *dst, const void *src, size_t len) {
    return pmem_memcpy(dst, src, len, PMEM_F_MEM_NONTEMPORAL);
}

#endif  // PM_UTILS_H_