add_executable(reliable_demo reliable_demo.cpp)
target_link_libraries(reliable_demo PRIVATE net reliable Threads::Threads)

add_executable(reliable_messages_demo reliable_messages_demo.cpp)
target_link_libraries(reliable_messages_demo PRIVATE net reliable Threads::Threads)

add_executable(mixed_channels_demo mixed_channels_demo.cpp)
target_link_libraries(mixed_channels_demo PRIVATE net reliable Threads::Threads)
