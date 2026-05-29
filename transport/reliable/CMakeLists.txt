add_library(reliable connection.cpp reliable_channel.cpp)
target_include_directories(reliable PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
