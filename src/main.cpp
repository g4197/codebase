#include "rdma/resource.h"

int main() {
    rdma::Context context;
    context.printDeviceInfoEx();
    return 0;
}