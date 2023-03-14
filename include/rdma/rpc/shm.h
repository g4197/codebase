#ifndef RDMA_RPC_SHM_H_
#define RDMA_RPC_SHM_H_

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>

#include "rdma/rpc/common.h"

namespace rdma {

// Don't overlap!
struct ShmRpcRing {
    static ShmRpcRing *create(const std::string &name, uint64_t n) {
        int fd = shm_open(name.c_str(), O_CREAT | O_RDWR, 0666);
        DLOG(INFO) << "shm_open " << name << " fd " << fd << " n " << n << " size "
                   << sizeof(ShmRpcRing) + n * sizeof(ShmRpcRingSlot);
        uint64_t size = sizeof(ShmRpcRing) + n * sizeof(ShmRpcRingSlot);
        if (ftruncate(fd, size) == -1) {
            LOG(FATAL) << "ftruncate for shm failed";
        }
        void *ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        memset(ptr, 0, size);
        if (ptr == MAP_FAILED) {
            LOG(FATAL) << "mmap for shm failed";
        }
        close(fd);
        ShmRpcRing *ring = (ShmRpcRing *)ptr;
        ring->ticket_ = 0;
        ring->n_ = n;
        return ring;
    }

    static ShmRpcRing *open(const std::string &name, uint64_t n) {
        int fd = shm_open(name.c_str(), O_RDWR, 0666);
        DLOG(INFO) << "shm_open " << name << " fd " << fd << " n " << n << " size "
                   << sizeof(ShmRpcRing) + n * sizeof(ShmRpcRingSlot);
        uint64_t size = sizeof(ShmRpcRing) + n * sizeof(ShmRpcRingSlot);
        void *ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (ptr == MAP_FAILED) {
            LOG(FATAL) << "mmap for shm failed";
        }
        close(fd);
        return (ShmRpcRing *)ptr;
    }

    inline std::atomic<uint64_t> *ticket() {
        return reinterpret_cast<std::atomic<uint64_t> *>(&ticket_);
    }

    inline uint64_t clientSend(uint8_t rpc_id, MsgBuf *send_buf) {
        uint64_t cur = ticket()->fetch_add(1);
        ShmRpcRingSlot *slot = &slots[idx(cur)];
        slot->rpc_id = rpc_id;
        slot->recv_buf = *send_buf;
        slot->turn()->fetch_add(1);
        slot->finished()->store(false);
        slot->turn()->fetch_add(1, std::memory_order_release);
        return cur;
    }

    inline void clientRecv(MsgBuf *recv_buf, uint64_t ticket) {
        ShmRpcRingSlot *slot = &slots[idx(ticket)];
        while (slot->finished()->load(std::memory_order_acquire) == false) {}
        recv_buf->size = slot->send_buf.size;
        memcpy(recv_buf->buf, slot->send_buf.buf, slot->send_buf.size);
        slot->finished()->store(false);
    }

    inline bool clientTryRecv(MsgBuf *recv_buf, uint64_t ticket) {
        ShmRpcRingSlot *slot = &slots[idx(ticket)];
        if (slot->finished()->load(std::memory_order_acquire) == false) {
            return false;
        }
        recv_buf->size = slot->send_buf.size;
        memcpy(recv_buf->buf, slot->send_buf.buf, slot->send_buf.size);
        slot->finished()->store(false);
        return true;
    }

    inline void serverRecv(uint64_t ticket) {
        uint64_t expected = 2 * turn(ticket) + 2;
        ShmRpcRingSlot *slot = &slots[idx(ticket)];
        while (slot->turn()->load(std::memory_order_acquire) != expected) {}
    }

    inline bool serverTryRecv(uint64_t ticket) {
        uint64_t expected = 2 * turn(ticket) + 2;
        ShmRpcRingSlot *slot = &slots[idx(ticket)];
        return slot->turn()->load(std::memory_order_acquire) == expected;
    }

    inline void serverSend(uint64_t ticket) {
        ShmRpcRingSlot *slot = &slots[idx(ticket)];
        slot->finished()->store(true);
    }

    inline ShmRpcRingSlot *get(uint64_t ticket) {
        return &slots[idx(ticket)];
    }

    uint64_t ticket_;
    uint64_t n_;
    ShmRpcRingSlot slots[];

private:
    inline uint64_t idx(uint64_t ticket) {
        return ticket % n_;
    }

    inline uint64_t turn(uint64_t ticket) {
        return ticket / n_;
    }
};
};  // namespace rdma

#endif  // RDMA_RPC_SHM_H_