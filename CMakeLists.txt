cmake_minimum_required(VERSION 3.8)

project(RDMA-examples)

# 设置C语言版本
set(CMAKE_C_STANDARD 11)
# 设置优化选项
set(CMAKE_C_FLAGS "-O2")
# 设置-flto
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -flto")

include(CheckCCompilerFlag)
CHECK_C_COMPILER_FLAG("-march=native" COMPILER_SUPPORTS_MARCH_NATIVE)
if (COMPILER_SUPPORTS_MARCH_NATIVE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=native")
endif ()

CHECK_C_COMPILER_FLAG("-mtune=native" COMPILER_SUPPORTS_MTUNE_NATIVE)
if (COMPILER_SUPPORTS_MTUNE_NATIVE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mtune=native")
endif ()

# 引入rdmacm库
find_library(RDMACM_LIB rdmacm)

# 引入libibverbs库
find_library(IBVERBS_LIB ibverbs)

# 头文件目录
include_directories(include)

add_executable(client src/client.c)
target_link_libraries(client ${RDMACM_LIB} ${IBVERBS_LIB})

add_executable(server src/server.c)
target_link_libraries(server ${RDMACM_LIB} ${IBVERBS_LIB})