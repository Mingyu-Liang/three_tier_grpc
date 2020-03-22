project(grpc-cmake-example)
cmake_minimum_required(VERSION 3.5)

add_compile_options(-std=c++11)

# GRPC and Protocol Buffers libraries location
list(APPEND CMAKE_PREFIX_PATH "/opt/grpc" "/opt/protobuf")

set(CMAKE_INSTALL_PREFIX /usr/local/bin)

# Cmake find modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

find_package(Protobuf REQUIRED)
find_package(GRPC REQUIRED)
find_package(nlohmann_json 3.5.0 REQUIRED)

set(PROTOS
  ${CMAKE_CURRENT_SOURCE_DIR}/protos/helloworld.proto
)

set(PROTO_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/protos/)
include_directories(${PROTO_SRC_DIR} /usr/local/include/jaegertracing)

set(GRPC_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/protos/helloworld.grpc.pb.cc)
set(PROTO_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/protos/helloworld.pb.cc)

# file(MAKE_DIRECTORY ${PROTO_SRC_DIR})
# protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_SRC_DIR} ${PROTOS})
# grpc_generate_cpp(GRPC_SRCS GRPC_HDRS ${PROTO_SRC_DIR} ${PROTOS})

add_subdirectory(src)