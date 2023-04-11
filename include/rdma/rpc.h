#ifndef RDMA_RPC_H_
#define RDMA_RPC_H_

#include "rdma/rpc/common.h"
#ifdef USE_RC_RPC
#include "rdma/rpc/rc_rpc.h"
#else
#include "rdma/rpc/rpc.h"
#endif
#include "rdma/rpc/shm.h"

#endif  // RDMA_RPC_H_