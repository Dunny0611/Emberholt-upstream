# Experimental Playerbot V2 port. Off unless -DBUILD_PLAYERBOT_V2=1.
# Does not link the v0.1.33 road graph, ClassicArmor, or ClassicSpellValue.

option(BUILD_PLAYERBOT_V2 "Build the experimental Playerbot V2 port" 0)
if(NOT BUILD_PLAYERBOT_V2)
  return()
endif()

add_library(playerbot-v2 STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/PlayerbotModule.cpp)

target_include_directories(playerbot-v2
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(playerbot-v2
  PRIVATE
    trinity-core-interface
  PUBLIC
    game-interface)

trinity_add_module(playerbot-v2 AddSC_playerbot_v2
  CONFIGS
    ${CMAKE_CURRENT_SOURCE_DIR}/conf/playerbot.conf.dist)

message(STATUS "Experimental Playerbot V2 module enabled (creation policy only, no world placement)")
