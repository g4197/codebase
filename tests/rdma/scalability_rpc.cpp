#include <bits/stdc++.h>

#include "stdrdma.h"
#include "stdutils.h"

using namespace rdma;
using namespace std;

// Understand the scalability of RPC
// read or write

constexpr char kClientIP[] = "10.0.2.172";
constexpr char kServerIP[] = "10.0.2.172";
int kClientThreads = 0;
int kServerThreads = 0;
int clt_numa = 2;
int srv_numa = 0;
TotalOp total_op[512];

int main(int argc, char **argv) {
    int is_server = false;
    if (argc > 1) {
        is_server = atoi(argv[1]);
        kClientThreads = atoi(argv[2]);
        kServerThreads = atoi(argv[3]);
        clt_numa = atoi(argv[4]);
        srv_numa = atoi(argv[5]);
    }
    if (!is_server) {
        Benchmark bm = Benchmark::run((Benchmark::BindCoreStrategy)clt_numa, kClientThreads, total_op, [&]() {
            RpcContext client_ctx(kClientIP, 10000 + my_thread_id, 1 + my_thread_id, 0, 0, -1);
            Rpc rpc(&client_ctx, nullptr, 0);
            LOG(INFO) << "Connect to " << kServerIP << ":" << 20000 + (my_thread_id % kServerThreads);
            RpcSession session = rpc.connect(kServerIP, 20000 + (my_thread_id % kServerThreads), 0);
            LOG(INFO) << "Connect finished";
            MsgBufPair *buf[32];
            for (int i = 0; i < 32; ++i) {
                buf[i] = new MsgBufPair(&client_ctx);
            }
            uint64_t cur = 0;
            while (true) {
                for (int i = 0; i < 32; ++i) {
                    buf[i]->send_buf->size = 64;
                    rpc.send(&session, 6, buf[i]);
                }
                for (int i = 0; i < 32; ++i) {
                    rpc.recv(buf[i]);
                }
                total_op[my_thread_id].ops += 32;
            }
        });
        bm.printTputAndJoin();
    } else {
        Thread t[kServerThreads];
        for (int i = 0; i < kServerThreads; ++i) {
            t[i] = Thread(srv_numa, [&]() {
                LOG(INFO) << "Server context created with " << kServerIP << ":" << 20000 + my_thread_id;
                RpcContext server_ctx(kServerIP, 20000 + my_thread_id, 0, 0, 0, -1);
                server_ctx.regFunc(6, [](ReqHandle *req, void *context) {
                    // memcpy(req->buf->send_buf->buf, req->buf->recv_buf->buf, req->buf->recv_buf->size);
                    req->buf->send_buf->size = req->buf->recv_buf->size;
                    req->response();
                });
                Rpc server_rpc(&server_ctx, nullptr, 0);
                LOG(INFO) << "Polling...";
                while (true) {
                    server_rpc.runServerLoopOnce();
                }
            });
        }
        while (true) sleep(1);
    }
}