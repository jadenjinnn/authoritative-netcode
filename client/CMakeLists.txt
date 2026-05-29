add_executable(client main.cpp)
target_link_libraries(client PRIVATE net Threads::Threads)
