add_library(bot_driver bot.cpp)
target_include_directories(bot_driver PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bot_driver PUBLIC sim)

add_executable(bots main.cpp)
target_link_libraries(bots PRIVATE bot_driver net reliable sim Threads::Threads)
