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
	# -O3
	-g
)

add_definitions(
	# -DNDEBUG
	# -DNO_DESTRUCT
)

link_libraries(
	atomic
	boost_coroutine
	boost_context
	ibverbs
    memkind
	jemalloc
	glog
	pmem
	memcached
	pthread
	numa
)

file(GLOB RDMA_LIB_SRC ${PROJECT_SOURCE_DIR}/src/rdma/*.cpp)
file(GLOB UTILS_LIB_SRC ${PROJECT_SOURCE_DIR}/src/utils/*.cpp)

add_library(
	librdma
	STATIC
	${RDMA_LIB_SRC}
	${UTILS_LIB_SRC}
)

add_executable(
	main
	${PROJECT_SOURCE_DIR}/src/main.cpp
)

target_link_libraries(
	main
	librdma
)