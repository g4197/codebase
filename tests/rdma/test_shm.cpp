#include <thread>

#include "stdrdma.h"
#include "stdutils.h"
#include "unistd.h"

using namespace rdma;

constexpr int kThreads = 16;
int main(int argc, char **argv) {
    Thread t(0, []() {
        ShmRpcRing *ring = ShmRpcRing::create("123", 32);
        uint64_t ticket = 0;
        while (true) {
            ring->serverRecv(ticket);
            ring->serverSend(ticket);
            ++ticket;
        }
    });
    sleep(1);
    TotalOp total_op[kThreads];
    Benchmark::run(Benchmark::kNUMA0, kThreads, total_op, [&]() {
        ShmRpcRing *ring = ShmRpcRing::open("123", 32);
        MsgBuf buf;
        buf.size = 4;
        while (true) {
            uint64_t cur = ring->clientSend(1, &buf);
            ring->clientRecv(&buf, cur);
            total_op[my_thread_id - 1].ops++;
        }
    }).printTputAndJoin();
}