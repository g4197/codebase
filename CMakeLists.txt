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
	-g
	-O2
)

add_definitions(
	-DNDEBUG
	-DNO_EX_VERBS
)

if (DEFINED USE_RC_RPC)
message("Use RC RPC")
add_definitions(
	-DUSE_RC_RPC
)
file(GLOB RDMA_LIB_SRC ${PROJECT_SOURCE_DIR}/src/rdma/context.cpp ${PROJECT_SOURCE_DIR}/src/rdma/qp.cpp ${PROJECT_SOURCE_DIR}/src/rdma/rc_rpc.cpp)
else()
message("Use UD RPC")
file(GLOB RDMA_LIB_SRC ${PROJECT_SOURCE_DIR}/src/rdma/context.cpp ${PROJECT_SOURCE_DIR}/src/rdma/qp.cpp ${PROJECT_SOURCE_DIR}/src/rdma/rpc.cpp)
endif()

file(GLOB STDUTILS_LIB_SRC ${PROJECT_SOURCE_DIR}/src/utils/stdutils_defs.cpp ${PROJECT_SOURCE_DIR}/src/utils/city.cc)
file(GLOB EXTUTILS_LIB_SRC ${PROJECT_SOURCE_DIR}/src/utils/extutils_defs.cpp)

add_library(
	rdma
	STATIC
	${RDMA_LIB_SRC}
)

target_link_libraries(
	rdma
	ibverbs
	pthread
	rt
)

add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/rpclib)
include_directories(${PROJECT_SOURCE_DIR}/third_party/rpclib/include)

add_library(
	stdutils
	STATIC
	${STDUTILS_LIB_SRC}
)

target_link_libraries(
	stdutils
	atomic
	jemalloc
	pthread
	numa
)

add_library(
	extutils
	STATIC
	${EXTUTILS_LIB_SRC}
)

target_link_libraries(
	extutils
	boost_coroutine
	boost_context
    memkind
	memcached
)

# add_executable(
# 	main
# 	${PROJECT_SOURCE_DIR}/src/main.cpp
# )

# target_link_libraries(
# 	main
# 	rdma
# 	stdutils
# )

add_subdirectory(tests)
