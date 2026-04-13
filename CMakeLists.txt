cmake_minimum_required(VERSION 3.20)
project(gds_benchmark LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CUDAToolkit REQUIRED)

# `CUDA::cuFile` is not available in some CMake/CUDA combinations.
# Resolve a compatible cuFile target explicitly so configure does not fail.
if(TARGET CUDA::cuFile)
  set(CUFILE_TARGET CUDA::cuFile)
elseif(TARGET CUDA::cufile)
  set(CUFILE_TARGET CUDA::cufile)
else()
  find_library(CUFILE_LIBRARY
    NAMES cufile libcufile
    HINTS
      ${CUDAToolkit_LIBRARY_ROOT}
      ${CUDAToolkit_LIBRARY_DIR}
      ${CUDAToolkit_LIBRARY_ROOT}/stubs
    PATH_SUFFIXES lib lib64 targets/x86_64-linux/lib
  )

  if(NOT CUFILE_LIBRARY)
    message(FATAL_ERROR
      "cuFile library not found. Install GPUDirect Storage userspace libs "
      "or provide CUFILE_LIBRARY path.")
  endif()

  add_library(CUDA::cuFile UNKNOWN IMPORTED)
  set_target_properties(CUDA::cuFile PROPERTIES IMPORTED_LOCATION "${CUFILE_LIBRARY}")
  set(CUFILE_TARGET CUDA::cuFile)
endif()

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
    ${CUFILE_TARGET}
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
    ${CUFILE_TARGET}
)

target_compile_features(gds_microbench PRIVATE cxx_std_17)

add_executable(gds_imagenet_bench
  src/imagenet_compare_main.cpp
)

target_link_libraries(gds_imagenet_bench
  PRIVATE
    CUDA::cudart
    ${CUFILE_TARGET}
)

target_compile_features(gds_imagenet_bench PRIVATE cxx_std_17)
