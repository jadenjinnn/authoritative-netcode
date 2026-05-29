add_library(net udp_socket.cpp sim_socket.cpp)
target_include_directories(net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
