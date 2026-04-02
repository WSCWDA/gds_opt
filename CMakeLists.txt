cmake_minimum_required(VERSION 3.20)
project(gds_benchmark LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CUDAToolkit REQUIRED)

add_library(gds_runners STATIC
  src/cpu_path_runner.cpp
  src/naive_gds_runner.cpp
  src/optimized_gds_runner.cpp
  src/gds_cache.cpp
  src/cached_gds_runner.cpp
)

target_include_directories(gds_runners
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(gds_runners
  PUBLIC
    CUDA::cudart
    CUDA::cuFile
)

add_executable(gds_bench
  src/bench_main.cpp
)

target_link_libraries(gds_bench
  PRIVATE
    gds_runners
)

add_executable(gds_microbench
  src/microbench_main.cpp
)

target_link_libraries(gds_microbench
  PRIVATE
    CUDA::cudart
    CUDA::cuFile
)

target_compile_features(gds_microbench PRIVATE cxx_std_17)
