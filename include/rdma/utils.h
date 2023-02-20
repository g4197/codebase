#ifndef RDMA_UTILS_H_
#define RDMA_UTILS_H_
#include <infiniband/verbs.h>

#include <regex>
#include <tuple>

#include "utils/log.h"

namespace rdma {
using namespace std;

uint8_t numa_aware_nic_port(int numa_node) {
    /*
     * GET the port number of IB NIC that matches param numa_node.
     * step 1. ibdev2netdev to get all NICs's net device
     * step 2. query /sys/class/net/{net device}/device/numa_node to get NUMA node
     * step 3. compare and find port.
     * Note: this function is not thread-safe and not compatiable to run concurrently with other ibverbs.
     * Note: this function's cost is a little high for it will read & write files repeatedly.
     * Best practice: get port ahead of time, then use other functions with ibverbs.
     */
    int num_devices;
    ibv_device **dev_list = ibv_get_device_list(&num_devices);
    FILE *fp = popen("ibdev2netdev", "r");
    char buf[1024] = { 0 };
    std::ignore = fread(buf, sizeof(buf), 1, fp);
    std::string str(buf);
    pclose(fp);

    int port_num = 0;

    for (int dev_i = 0; dev_i < num_devices; dev_i++) {
        ibv_context *ib_ctx = ibv_open_device(dev_list[dev_i]);
        ibv_device_attr device_attr;
        memset(&device_attr, 0, sizeof(device_attr));
        if (ibv_query_device(ib_ctx, &device_attr) != 0) {
            LOG(ERROR) << "Fail to query device " << dev_i;
        }
        const char *name = ibv_get_device_name(dev_list[dev_i]);
        char re_buf[1024] = { 0 };
        sprintf(re_buf, "%s.*?==> (.*?) ", name);
        regex re(re_buf);
        smatch sm;
        if (regex_search(str, sm, re)) {
            char path[1024] = { 0 };
            sprintf(path, "/sys/class/net/%s/device/numa_node", sm[1].str().c_str());
            FILE *fp = fopen(path, "r");
            char buf[1024] = { 0 };
            std::ignore = fgets(buf, sizeof(buf), fp);
            fclose(fp);
            int numa_id = atoi(buf);
            DLOG(INFO) << "Device " << name << " is on NUMA " << numa_id;
            if (numa_id == numa_node) {
                // NUMA node matches, find an available port.
                for (uint8_t port_i = 1; port_i <= device_attr.phys_port_cnt; port_i++) {
                    // Count this port only if it is enabled
                    ibv_port_attr port_attr;
                    if (ibv_query_port(ib_ctx, port_i, &port_attr) != 0) {
                        LOG(ERROR) << "Fail to query port " << port_i;
                    }

                    if (port_attr.phys_state != IBV_PORT_ACTIVE && port_attr.phys_state != IBV_PORT_ACTIVE_DEFER) {
                        continue;
                    } else {
                        DLOG(INFO) << "Found port number " << port_num + port_i - 1 << " on device " << name;
                        return port_num + port_i - 1;
                    }
                }
            }
        }
        port_num += device_attr.phys_port_cnt;
        if (ibv_close_device(ib_ctx) != 0) {
            LOG(ERROR) << "Fail to close device " << dev_i;
        }
    }
    DLOG(INFO) << "No way to find a NUMA-aware port for NUMA " << numa_node << ", fallback.\n";
    // Assume all the ports are active here (which may be wrong)
    return numa_node % port_num;
}
};      // namespace rdma
#endif  // RDMA_UTILS_H_