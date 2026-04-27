cmake_minimum_required(VERSION 3.20)
project(groute LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(groute_runtime STATIC
  cpp/src/metadata.cpp
  cpp/src/route_planner.cpp
  cpp/src/posix_backend.cpp
  cpp/src/cufile_stub_backend.cpp
  cpp/src/runtime.cpp
)
set_target_properties(groute_runtime PROPERTIES OUTPUT_NAME groute_runtime)

target_include_directories(groute_runtime
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/include
)

add_executable(groute_runtime_demo
  examples/runtime_demo.cpp
)

target_link_libraries(groute_runtime_demo PRIVATE groute_runtime)

include(CTest)
if(BUILD_TESTING)
  add_executable(test_metadata tests/test_metadata.cpp)
  target_link_libraries(test_metadata PRIVATE groute_runtime)
  add_test(NAME metadata COMMAND test_metadata)

  add_executable(test_route_planner tests/test_route_planner.cpp)
  target_link_libraries(test_route_planner PRIVATE groute_runtime)
  add_test(NAME route_planner COMMAND test_route_planner)
endif()
