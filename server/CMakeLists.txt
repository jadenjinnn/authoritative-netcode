add_executable(server main.cpp)
target_link_libraries(server PRIVATE net reliable sim Threads::Threads
    prometheus-cpp::core prometheus-cpp::pull)
