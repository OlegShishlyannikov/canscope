include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.4
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

# Pull the Catch2 CMake helpers (catch_discover_tests, etc.).
list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(Catch)

add_executable(canscope_tests
  test_parsers.cpp
  test_candump_parser.cpp
  test_bam_reassembler.cpp
  test_dm1_positions.cpp
  test_enum_tables.cpp
  ${CMAKE_SOURCE_DIR}/src/parsers.cpp
  ${CMAKE_SOURCE_DIR}/src/candump_parser.cpp
  ${CMAKE_SOURCE_DIR}/src/bam_reassembler.cpp
  ${CMAKE_SOURCE_DIR}/src/enum_tables.cpp
)

target_include_directories(canscope_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${fmt_SOURCE_DIR}/include
  ${Boost_INCLUDE_DIRS}
)

target_link_libraries(canscope_tests PRIVATE
  Catch2::Catch2WithMain
)

catch_discover_tests(canscope_tests)
