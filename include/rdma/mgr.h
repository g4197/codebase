#ifndef RPC_MGR_H_
#define RPC_MGR_H_

#include <string>

#include "pthread.h"
#include "rdma/predefs.h"
#include "rdma/simple_kv.h"
#include "rpc/client.h"
#include "rpc/server.h"
#include "utils/log.h"

namespace rdma {

inline std::string qp_key(int id) {
    return "qp_" + std::to_string(id);
}

inline std::string mr_key(int id) {
    return "mr_" + std::to_string(id);
}

class ManagerServer {
public:
    ManagerServer(const std::string &ip, int port) : srv_(ip, port) {
        srv_.bind("get", [&](const std::string &key) {
            DLOG(INFO) << "Server " << ip << ":" << port << " Received get " << key;
            return kv_.get(key);
        });
        srv_.async_run(1);
    }

    ~ManagerServer() {
        srv_.stop();
    }

    void put(const std::string &key, const std::string &value) {
        return kv_.put(key, value);
    }

    void putQPInfo(int id, const QPInfo &info) {
        put(qp_key(id), std::string((char *)&info, sizeof(QPInfo)));
    }

    void putMRInfo(int id, const MRInfo &info) {
        put(mr_key(id), std::string((char *)&info, sizeof(MRInfo)));
    }

private:
    rpc::server srv_;
    SimpleKV<std::string, std::string> kv_;
};

class ManagerClient {
public:
    ManagerClient() {}

    ManagerClient(const std::string &ip, int port) : cli_(new rpc::client(ip, port)) {}

    std::string get(const std::string &key) {
        DLOG(INFO) << "Sending get " << key;
        if (!cache_.exists(key)) {
            cache_.put(key, cli_->call("get", key).as<std::string>());
        }
        DLOG(INFO) << "Sending get finished";
        return cache_.get(key);
    }

    QPInfo getQPInfo(int id) {
        std::string value = get(qp_key(id));
        QPInfo info;
        memset(&info, 0, sizeof(info));
        if (value.empty()) {
            return info;
        }
        memcpy(&info, value.data(), sizeof(info));
        return info;
    }

    MRInfo getMRInfo(int id) {
        std::string value = get(mr_key(id));
        MRInfo info;
        memset(&info, 0, sizeof(info));
        if (value.empty()) {
            return info;
        }
        memcpy(&info, value.data(), sizeof(info));
        return info;
    }

private:
    rpc::client *cli_;
    SimpleKV<std::string, std::string> cache_;  // Note: assume that QPInfo and MRInfo are immutable
};
}  // namespace rdma

#endif  // RPC_MGR_H_