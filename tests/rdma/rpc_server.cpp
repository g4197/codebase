#include "stdrdma.h"
#include "stdutils.h"

constexpr char *client_ip = "10.0.2.172";
constexpr int client_port = 1234;
constexpr char *server_ip = "10.0.2.175";
constexpr int server_port = 1234;

using namespace rdma;
ReqHandle *reqs[64];
int n = 0;

int main() {
    RpcContext server_ctx(server_ip, server_port, 0, 0, 0, -1);
    server_ctx.ctx.printDeviceInfoEx();
    server_ctx.regFunc(6, [](ReqHandle *req, void *context) {
        memcpy(req->buf->send_buf->buf, req->buf->recv_buf->buf, req->buf->recv_buf->size);
        req->buf->send_buf->size = req->buf->recv_buf->size;
        reqs[n++] = req;
        if (n == 64) {
            int x[64];
            for (int i = 0; i < 64; ++i) {
                x[i] = i;
            }
            std::random_shuffle(x, x + 64);
            for (int i = 0; i < 64; ++i) reqs[x[i]]->response();
            n = 0;
        }
    });
    Rpc server_rpc(&server_ctx, nullptr, 0);
    LOG(INFO) << "Polling...";
    while (true) {
        server_rpc.runServerLoopOnce();
    }
}
