cmake_minimum_required(VERSION 3.20)
project(authoritative-netcode CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

enable_testing()

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PUSH OFF CACHE BOOL "" FORCE)
set(ENABLE_COMPRESSION OFF CACHE BOOL "" FORCE)
# Compression is off, so zlib isn't needed; stop find_package(ZLIB) from caching a
# libz.so dev-symlink path that doesn't exist without zlib1g-dev installed.
set(CMAKE_DISABLE_FIND_PACKAGE_ZLIB ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  prometheus-cpp
  GIT_REPOSITORY https://github.com/jupp0r/prometheus-cpp.git
  GIT_TAG v1.2.4
)
FetchContent_MakeAvailable(prometheus-cpp)

find_package(Threads REQUIRED)

# Subdirectories are added per phase as modules land.
add_subdirectory(transport/net)
add_subdirectory(transport/reliable)
add_subdirectory(transport/examples)
add_subdirectory(sim)
add_subdirectory(server)
add_subdirectory(client)
add_subdirectory(bots)
add_subdirectory(tests)
