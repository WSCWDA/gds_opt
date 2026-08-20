cmake_minimum_required(VERSION 3.20)
project(gds_hcache LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GHC_ENABLE_CUDA "Enable CUDA pinned memory and H2D copies" ON)
option(GHC_ENABLE_GDS "Enable the cuFile large-I/O backend" ON)
option(GHC_ENABLE_URING "Enable io_uring fixed-buffer cache fills" ON)
option(GHC_BUILD_TESTS "Build dependency-free cache tests" ON)

find_package(Threads REQUIRED)

add_library(ghc_core STATIC
  src/cache.cpp
)
target_include_directories(ghc_core PUBLIC include PRIVATE src)
target_link_libraries(ghc_core PUBLIC Threads::Threads)
target_compile_options(ghc_core PRIVATE -Wall -Wextra -Wpedantic)

if(GHC_ENABLE_CUDA)
  find_package(CUDAToolkit REQUIRED)
endif()

if(GHC_ENABLE_URING)
  find_path(LIBURING_INCLUDE_DIR liburing.h REQUIRED)
  find_library(LIBURING_LIBRARY uring REQUIRED)
endif()

if(GHC_ENABLE_GDS)
  if(NOT GHC_ENABLE_CUDA)
    message(FATAL_ERROR "GHC_ENABLE_GDS requires GHC_ENABLE_CUDA")
  endif()
  find_path(CUFILE_INCLUDE_DIR cufile.h
    HINTS ${CUDAToolkit_INCLUDE_DIRS} /usr/local/cuda/include REQUIRED)
  find_library(CUFILE_LIBRARY cufile
    HINTS /usr/local/cuda/lib64 /usr/local/cuda/gds/lib REQUIRED)
endif()

add_library(gds_hcache STATIC
  src/gds_hcache.cpp
  src/host_pool.cpp
  src/uring_backend.cpp
  src/cufile_backend.cpp
)
target_include_directories(gds_hcache
  PUBLIC include
  PRIVATE src ${LIBURING_INCLUDE_DIR} ${CUFILE_INCLUDE_DIR})
target_link_libraries(gds_hcache PUBLIC ghc_core)
target_compile_definitions(gds_hcache PUBLIC
  GHC_ENABLE_CUDA=$<BOOL:${GHC_ENABLE_CUDA}>
  GHC_ENABLE_GDS=$<BOOL:${GHC_ENABLE_GDS}>
  GHC_ENABLE_URING=$<BOOL:${GHC_ENABLE_URING}>)
target_compile_options(gds_hcache PRIVATE -Wall -Wextra -Wpedantic)

if(GHC_ENABLE_CUDA)
  target_link_libraries(gds_hcache PUBLIC CUDA::cudart)
endif()
if(GHC_ENABLE_GDS)
  target_link_libraries(gds_hcache PUBLIC ${CUFILE_LIBRARY})
endif()
if(GHC_ENABLE_URING)
  target_link_libraries(gds_hcache PUBLIC ${LIBURING_LIBRARY})
endif()

add_executable(ghc_bench bench/ghc_bench.cpp)
target_link_libraries(ghc_bench PRIVATE gds_hcache)
target_compile_definitions(ghc_bench PRIVATE GHC_ENABLE_CUDA=$<BOOL:${GHC_ENABLE_CUDA}>)
if(GHC_ENABLE_CUDA)
  target_link_libraries(ghc_bench PRIVATE CUDA::cudart)
endif()

if(GHC_BUILD_TESTS)
  enable_testing()
  add_executable(ghc_cache_test tests/cache_test.cpp)
  target_include_directories(ghc_cache_test PRIVATE src)
  target_link_libraries(ghc_cache_test PRIVATE ghc_core)
  add_test(NAME ghc_cache_test COMMAND ghc_cache_test)
endif()
