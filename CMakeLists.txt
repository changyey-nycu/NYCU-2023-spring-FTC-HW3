# Copyright 2018 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# cmake build file for C++ route_guide example.
# Assumes protobuf and gRPC have been installed using cmake.
# See cmake_externalproject/CMakeLists.txt for all-in-one cmake build
# that automatically builds all the dependencies before building route_guide.

cmake_minimum_required(VERSION 3.5.1)

project(eVoting C CXX)

include(./cmake/common.cmake)

# Proto file
get_filename_component(eVoting "./protos/eVoting.proto" ABSOLUTE)
get_filename_component(eVoting_path "${eVoting}" PATH)

# Generated sources
set(eVoting_srcs "${CMAKE_CURRENT_BINARY_DIR}/eVoting.pb.cc")
set(eVoting_hdrs "${CMAKE_CURRENT_BINARY_DIR}/eVoting.pb.h")
set(eVoting_grpc_srcs "${CMAKE_CURRENT_BINARY_DIR}/eVoting.grpc.pb.cc")
set(eVoting_grpc_hdrs "${CMAKE_CURRENT_BINARY_DIR}/eVoting.grpc.pb.h")
add_custom_command(
      OUTPUT "${eVoting_srcs}" "${eVoting_hdrs}" "${eVoting_grpc_srcs}" "${eVoting_grpc_hdrs}"
      COMMAND ${_PROTOBUF_PROTOC}
      ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
        --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
        -I "${eVoting_path}"
        --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
        "${eVoting}"
      DEPENDS "${eVoting}")

# Include generated *.pb.h files
include_directories("${CMAKE_CURRENT_BINARY_DIR}")

# eVoting_grpc_proto
add_library(eVoting_grpc_proto
  ${eVoting_grpc_srcs}
  ${eVoting_grpc_hdrs}
  ${eVoting_srcs}
  ${eVoting_hdrs})
target_link_libraries(eVoting_grpc_proto
  ${_REFLECTION}
  ${_GRPC_GRPCPP}
  ${_PROTOBUF_LIBPROTOBUF})

# Targets eVoting_(client|server)
set(_LSODIUM sodium)
set(_PQ pq)
set(_PQXX pqxx)

foreach(_target
  eVoting_client eVoting_server eVoting_backup)
  add_executable(${_target}
    "${_target}.cc")
  target_link_libraries(${_target}
  eVoting_grpc_proto
    ${_REFLECTION}
    ${_GRPC_GRPCPP}
    ${_PROTOBUF_LIBPROTOBUF}
    ${_LSODIUM}
    ${_PQ}
    ${_PQXX})
endforeach()

