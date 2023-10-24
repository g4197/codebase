add_executable(coherence ${PROJECT_SOURCE_DIR}/tests/think/coherence.cpp)

target_link_libraries(
	coherence
	rdma
	stdutils
	rpc
	profiler
)

add_executable(rdma_op ${PROJECT_SOURCE_DIR}/tests/think/rdma_op.cpp)

target_link_libraries(
	rdma_op
	rdma
	stdutils
	rpc
	profiler
)

add_executable(scalability ${PROJECT_SOURCE_DIR}/tests/think/scalability.cpp)

target_link_libraries(
	scalability
	rdma
	stdutils
	rpc
	profiler
)

add_executable(mem ${PROJECT_SOURCE_DIR}/tests/think/mem.cpp)
target_link_libraries(
	mem
	stdutils
)