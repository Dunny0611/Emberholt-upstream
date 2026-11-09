# This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
#
# This file is free software; as a special exception the author gives
# unlimited permission to copy and/or distribute it, with or without
# modifications, as long as this notice is preserved.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY, to the extent permitted by law; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

GroupSources(${CMAKE_CURRENT_SOURCE_DIR})

add_executable(tests)

CollectAndAddSourceFiles(
  tests
  ${CMAKE_CURRENT_SOURCE_DIR})

set(PLAYERBOT_TEST_LIB)
if(BUILD_PLAYERBOT_V2)
  set(PLAYERBOT_TEST_LIB playerbot-v2)
  target_compile_definitions(tests PRIVATE TRINITY_PLAYERBOT_V2=1)
endif()

target_link_libraries(tests
  PRIVATE
    trinity-core-interface
    ${PLAYERBOT_TEST_LIB}
    game
    Catch2::Catch2WithMain)

target_include_directories(tests
  PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/src/modules/PlayerbotV2)

catch_discover_tests(tests)

set_target_properties(tests
    PROPERTIES
      FOLDER
        "tests")
