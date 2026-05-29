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

find_package(Threads REQUIRED)

# Subdirectories are added per phase as modules land.
add_subdirectory(transport/net)
add_subdirectory(transport/reliable)
add_subdirectory(transport/examples)
add_subdirectory(sim)
add_subdirectory(server)
add_subdirectory(client)
add_subdirectory(tests)
