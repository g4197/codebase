#include "rdma/resource.h"
#include "utils/type_traits.h"

int main() {
    rdma::Context context;
    context.printDeviceInfoEx();
    return 0;
}