#include "stdrdma.h"
#include "stdutils.h"

constexpr char *client_ip = "10.0.2.172";
constexpr int client_port = 1235;
constexpr char *server_ip = "10.0.2.172";
constexpr int server_port = 1234;

using namespace rdma;

int kThreads = 1;

int main() {
    TotalOp total_op[kThreads];
    Benchmark::run(Benchmark::kNUMA0, kThreads, total_op, [&]() {
        RpcContext client_ctx(client_ip, client_port + my_thread_id, 1 + my_thread_id);
        Rpc rpc(&client_ctx, nullptr, 0);
        RpcSession session = rpc.connect(server_ip, server_port, 0);
        MsgBufPair buf(&client_ctx);
        memcpy(buf.send_buf->buf, "hello, world", 5);
        buf.send_buf->size = 5;
        uint64_t cur = 0;
        while (true) {
            // ++cur;
            total_op[my_thread_id].ops++;
            // memcpy(buf.send_buf->buf, &cur, sizeof(cur));
            buf.send_buf->size = 4;
            rpc.send(&session, 6, &buf);
            rpc.recv(&buf);
            // if (cur != *(uint64_t *)buf.recv_buf->buf) {
            //     LOG(FATAL) << "cur = " << cur << ", recv = " << *(uint64_t *)buf.recv_buf->buf;
            // }
        }
    }).printTputAndJoin();
}