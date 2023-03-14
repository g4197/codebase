add_executable(test_rdma ${PROJECT_SOURCE_DIR}/tests/rdma/test_rdma.cpp)

target_link_libraries(
	test_rdma
	rdma
	utils
	rpc
)

add_executable(rpc_client ${PROJECT_SOURCE_DIR}/tests/rdma/rpc_client.cpp)

target_link_libraries(
	rpc_client
	rdma
	utils
	rpc
)

add_executable(rpc_server ${PROJECT_SOURCE_DIR}/tests/rdma/rpc_server.cpp)

target_link_libraries(
	rpc_server
	rdma
	utils
	rpc
	profiler
)

add_executable(test_shm ${PROJECT_SOURCE_DIR}/tests/rdma/test_shm.cpp)

target_link_libraries(
	test_shm
	rdma
	utils
	rpc
	profiler
)