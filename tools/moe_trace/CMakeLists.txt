# Experiment-only ground-truth tool; never part of the moepager product path.
#   cmake -S tools/moe_trace -B build/moe_trace -DLLAMA_CPP_DIR=$HOME/src/llama.cpp
#   cmake --build build/moe_trace -j
cmake_minimum_required(VERSION 3.14)
project(moe_trace CXX C)
set(CMAKE_CXX_STANDARD 17)
set(LLAMA_CPP_DIR "" CACHE PATH "llama.cpp source checkout")
if(NOT LLAMA_CPP_DIR)
  message(FATAL_ERROR "set -DLLAMA_CPP_DIR=/path/to/llama.cpp")
endif()
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
add_subdirectory(${LLAMA_CPP_DIR} llama EXCLUDE_FROM_ALL)
add_executable(moe_trace moe_trace.cpp)
target_link_libraries(moe_trace PRIVATE llama ggml)
