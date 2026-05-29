add_executable(server main.cpp)
target_link_libraries(server PRIVATE net sim Threads::Threads)
