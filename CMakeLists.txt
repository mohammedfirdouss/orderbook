cmake_minimum_required(VERSION 3.20)
project(orderbook CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(OB_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_library(orderbook INTERFACE)
target_include_directories(orderbook INTERFACE include)
target_compile_options(orderbook INTERFACE -Wall -Wextra -Wpedantic)
if(OB_SANITIZE)
  target_compile_options(orderbook INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(orderbook INTERFACE -fsanitize=address,undefined)
endif()

add_executable(tests tests/tests.cpp)
target_link_libraries(tests PRIVATE orderbook)

enable_testing()
add_test(NAME tests COMMAND tests)
