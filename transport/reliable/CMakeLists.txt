add_library(reliable connection.cpp channel.cpp fragment.cpp rtt.cpp rate_governor.cpp)
target_include_directories(reliable PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
