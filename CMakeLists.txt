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
