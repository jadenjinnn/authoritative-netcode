add_library(sim fixed_timestep.cpp world.cpp snapshot.cpp snapshot_history.cpp)
target_include_directories(sim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
