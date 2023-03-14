#include <gperftools/profiler.h>
#include <signal.h>

#include "stdrdma.h"
#include "stdutils.h"

constexpr char *client_ip = "10.0.2.172";
constexpr int client_port = 1235;
constexpr char *server_ip = "10.0.2.172";
constexpr int server_port = 1234;

using namespace rdma;

void gprofStartAndStop(int signum) {
    static int isStarted = 0;
    if (signum != SIGUSR1) return;
    if (!isStarted) {
        isStarted = 1;
        ProfilerStart("prof/dm.prof");
        fprintf(stderr, "ProfilerStart success\n");
    } else {
        isStarted = 0;
        ProfilerStop();
        fprintf(stderr, "ProfilerStop success\n");
    }
}

int main() {
    signal(SIGUSR1, gprofStartAndStop);
    bind_to_core(0, 20);
    RpcContext server_ctx(server_ip, server_port, 0);
    server_ctx.regFunc(6, [](ReqHandle *req, void *context) {
        // memcpy(req->buf->send_buf->buf, req->buf->recv_buf->buf, req->buf->recv_buf->size);
        // req->buf->send_buf->size = req->buf->recv_buf->size;
        req->response();
    });
    Rpc server_rpc(&server_ctx, nullptr, 0);
    LOG(INFO) << "Polling...";
    while (true) {
        server_rpc.runServerLoopOnce();
    }
}