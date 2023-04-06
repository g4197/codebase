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
        MsgBufPair *buf[64];
        for (int i = 0; i < 64; ++i) {
            buf[i] = new MsgBufPair(&client_ctx);
        }
        while (true) {
            for (int i = 0; i < 64; ++i) {
                *(uint64_t *)buf[i]->send_buf->buf = i;
                buf[i]->send_buf->size = sizeof(uint64_t);
            }
            for (int i = 0; i < 64; ++i) {
                rpc.send(&session, 6, buf[i]);
            }
            for (int i = 0; i < 64; ++i) {
                rpc.recv(buf[i]);
                if (*(uint64_t *)buf[i]->recv_buf->buf != i) {
                    LOG(ERROR) << "Error: " << *(uint64_t *)buf[i]->recv_buf->buf;
                }
            }
            total_op[my_thread_id].ops += 64;
        }
    }).printTputAndJoin();
}
