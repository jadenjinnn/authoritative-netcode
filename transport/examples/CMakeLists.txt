add_executable(reliable_demo reliable_demo.cpp)
target_link_libraries(reliable_demo PRIVATE net reliable Threads::Threads)

add_executable(reliable_messages_demo reliable_messages_demo.cpp)
target_link_libraries(reliable_messages_demo PRIVATE net reliable Threads::Threads)

add_executable(mixed_channels_demo mixed_channels_demo.cpp)
target_link_libraries(mixed_channels_demo PRIVATE net reliable Threads::Threads)

add_executable(fragmentation_demo fragmentation_demo.cpp)
target_link_libraries(fragmentation_demo PRIVATE net reliable Threads::Threads)

add_executable(rtt_demo rtt_demo.cpp)
target_link_libraries(rtt_demo PRIVATE net reliable Threads::Threads)

add_executable(rate_governor_demo rate_governor_demo.cpp)
target_link_libraries(rate_governor_demo PRIVATE reliable)
