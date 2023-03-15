add_executable(coherence ${PROJECT_SOURCE_DIR}/tests/think/coherence.cpp)

target_link_libraries(
	coherence
	rdma
	utils
	rpc
	profiler
)