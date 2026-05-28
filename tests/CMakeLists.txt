include(GoogleTest)

add_executable(sim_tests fixed_timestep_test.cpp)
target_link_libraries(sim_tests PRIVATE sim GTest::gtest_main)
gtest_discover_tests(sim_tests)
