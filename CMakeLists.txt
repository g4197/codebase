cmake_minimum_required(VERSION 3.13)
project (rdma-wrapper)

set(PROJECT_BINARY_DIR ${PROJECT_SOURCE_DIR}/build)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/build/lib)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/build/bin)

include_directories(
	${PROJECT_SOURCE_DIR}/include
	/usr/local/include
)

add_compile_options(
	-Wall
	-Wextra
    -fno-strict-aliasing
	-std=c++17
	-march=native
	-O3
	-g
)

link_libraries(
	ibverbs
    memkind
	jemalloc
	glog
	pmem
)
