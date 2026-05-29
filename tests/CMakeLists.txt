include(GoogleTest)

add_executable(sim_tests fixed_timestep_test.cpp)
target_link_libraries(sim_tests PRIVATE sim GTest::gtest_main)
gtest_discover_tests(sim_tests)

add_executable(net_tests udp_socket_test.cpp)
target_link_libraries(net_tests PRIVATE net GTest::gtest_main)
gtest_discover_tests(net_tests)
