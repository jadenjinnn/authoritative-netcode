cmake_minimum_required(VERSION 3.20)
project(netcode CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

enable_testing()

# Subdirectories are added per phase as modules land (transport/, server/, client/, ...).
