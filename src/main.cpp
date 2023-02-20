#include <atomic>
#include <iostream>

#include "stdutils.h"

int main() {
    Thread thr[4];
    for (int i = 0; i < 4; ++i) {
        thr[i] = Thread(0, []() {
            Memcache memc;
            memc.connect("memcached.conf");
            LOG(INFO) << "Hello from thread " << my_thread_id;
            memc.barrier("barrier", 4);
            LOG(INFO) << "Barrier passed in thread " << my_thread_id;
        });
    }
    // for (int i = 0; i < 4; ++i) {
    //     thr[i].join();
    // }
}