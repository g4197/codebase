add_executable(test_rdma ${PROJECT_SOURCE_DIR}/tests/rdma/test_rdma.cpp)

target_link_libraries(
	test_rdma
	rdma
	stdutils
	rpc
)

add_executable(rpc_client ${PROJECT_SOURCE_DIR}/tests/rdma/rpc_client.cpp)

target_link_libraries(
	rpc_client
	rdma
	stdutils
	rpc
)

add_executable(rpc_server ${PROJECT_SOURCE_DIR}/tests/rdma/rpc_server.cpp)

target_link_libraries(
	rpc_server
	rdma
	stdutils
	rpc
	profiler
)

add_executable(test_shm ${PROJECT_SOURCE_DIR}/tests/rdma/test_shm.cpp)
target_link_libraries(
	test_shm
	rdma
	stdutils
	rpc
)

add_executable(scalability_rpc ${PROJECT_SOURCE_DIR}/tests/rdma/scalability_rpc.cpp)
target_link_libraries(
	scalability_rpc
	rdma
	stdutils
	rpc
)

add_subdirectory(think)

add_executable(test_rwl ${PROJECT_SOURCE_DIR}/tests/test_rwl.cpp)
target_link_libraries(
	test_rwl
	rdma
	stdutils
	rpc
)