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
