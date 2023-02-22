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

add_definitions(
	# -DNDEBUG
	# -DNO_EX_VERBS
)

file(GLOB RDMA_LIB_SRC ${PROJECT_SOURCE_DIR}/src/rdma/*.cpp)
file(GLOB UTILS_LIB_SRC ${PROJECT_SOURCE_DIR}/src/utils/*.cpp ${PROJECT_SOURCE_DIR}/src/utils/*.cc)

add_library(
	rdma
	STATIC
	${RDMA_LIB_SRC}
)

target_link_libraries(
	rdma
	ibverbs
	pthread
)

add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/rpclib)
include_directories(${PROJECT_SOURCE_DIR}/third_party/rpclib/include)

add_library(
	utils
	STATIC
	${UTILS_LIB_SRC}
)

target_link_libraries(
	utils
	atomic
	boost_coroutine
	boost_context
    memkind
	jemalloc
	pmem
	memcached
	pthread
	numa
)

add_executable(
	main
	${PROJECT_SOURCE_DIR}/src/main.cpp
)

target_link_libraries(
	main
	rdma
	utils
)

add_executable(test_rdma ${PROJECT_SOURCE_DIR}/tests/test_rdma.cpp)

target_link_libraries(
	test_rdma
	rdma
	utils
	rpc
)

add_executable(test_cm ${PROJECT_SOURCE_DIR}/tests/test_cm.cpp)
target_link_libraries(test_cm rdma rdmacm utils rpc)