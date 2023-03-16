add_executable(coherence ${PROJECT_SOURCE_DIR}/tests/think/coherence.cpp)

target_link_libraries(
	coherence
	rdma
	utils
	rpc
	profiler
)

add_executable(cas_faa_dm ${PROJECT_SOURCE_DIR}/tests/think/cas_faa_dm.cpp)

target_link_libraries(
	cas_faa_dm
	rdma
	utils
	rpc
	profiler
)