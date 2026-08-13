cmake_minimum_required(VERSION 3.16)
project(diskann_real_io_support LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(diskann_io_trace
  src/io_bench/io_trace.cpp
  src/io_bench/query_context.cpp
  src/io_bench/io_stats.cpp)
target_include_directories(diskann_io_trace PUBLIC include)
target_compile_options(diskann_io_trace PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_executable(test_trace_roundtrip tests/io_bench/test_trace_roundtrip.cpp)
target_link_libraries(test_trace_roundtrip PRIVATE diskann_io_trace)
add_test(NAME trace_roundtrip COMMAND test_trace_roundtrip)
