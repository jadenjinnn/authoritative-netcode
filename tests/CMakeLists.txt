include(GoogleTest)

add_executable(sim_tests fixed_timestep_test.cpp)
target_link_libraries(sim_tests PRIVATE sim GTest::gtest_main)
gtest_discover_tests(sim_tests)

add_executable(net_tests udp_socket_test.cpp)
target_link_libraries(net_tests PRIVATE net GTest::gtest_main)
gtest_discover_tests(net_tests)

add_executable(sim_socket_tests sim_socket_test.cpp)
target_link_libraries(sim_socket_tests PRIVATE net GTest::gtest_main)
gtest_discover_tests(sim_socket_tests)

add_executable(connection_tests connection_test.cpp)
target_link_libraries(connection_tests PRIVATE reliable GTest::gtest_main)
gtest_discover_tests(connection_tests)

add_executable(reliable_channel_tests reliable_channel_test.cpp)
target_link_libraries(reliable_channel_tests PRIVATE reliable GTest::gtest_main)
gtest_discover_tests(reliable_channel_tests)

add_executable(mixed_channels_tests mixed_channels_test.cpp)
target_link_libraries(mixed_channels_tests PRIVATE reliable GTest::gtest_main)
gtest_discover_tests(mixed_channels_tests)

add_executable(fragment_tests fragment_test.cpp)
target_link_libraries(fragment_tests PRIVATE reliable GTest::gtest_main)
gtest_discover_tests(fragment_tests)
