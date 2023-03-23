#include "stdrdma.h"
#include "stdutils.h"

constexpr char *client_ip = "10.0.2.172";
constexpr int client_port = 1234;
constexpr char *server_ip = "10.0.2.175";
constexpr int server_port = 1234;

using namespace rdma;

int kThreads = 1;

int main() {
    TotalOp total_op[kThreads];
    Benchmark::run(Benchmark::kNUMA0, kThreads, total_op, [&]() {
        RpcContext client_ctx(client_ip, client_port + my_thread_id, 1 + my_thread_id, 0, 0, -1);
        client_ctx.ctx.printDeviceInfoEx();
        Rpc rpc(&client_ctx, nullptr, 0);
        RpcSession session = rpc.connect(server_ip, server_port, 0);
        MsgBufPair buf(&client_ctx);
        uint64_t cur = 0;
        while (true) {
            ++cur;
            total_op[my_thread_id].ops++;
            int len = sprintf((char *)buf.send_buf->buf,
                              "abcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcd%ld", cur);
            buf.send_buf->size = len;
            rpc.send(&session, 6, &buf);
            rpc.recv(&buf);
        }
    }).printTputAndJoin();
}
