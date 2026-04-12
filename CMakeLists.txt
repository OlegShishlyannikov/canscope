cmake_minimum_required(VERSION 3.13)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MESSAGE_LOG_LEVEL TRACE)
set(CMAKE_VERBOSE_MAKEFILE OFF)

if(NOT CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_CXX_COMPILER /usr/bin/clang++)
endif()

option(BUILD_SHARED_LIBS "Build using shared libraries" OFF)
include(cmake/dependencies.cmake)

project(canscope)
file(GLOB_RECURSE SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_executable(${CMAKE_PROJECT_NAME} ${SOURCES})
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/thirdparty
  ${ftxui_SOURCE_DIR}/include
  ${tpl_SOURCE_DIR}/include
  ${xlnt_SOURCE_DIR}/include
  ${fmt_SOURCE_DIR}/include
  ${json_SOURCE_DIR}/include
  ${sqlite_modern_SOURCE_DIR}/hdr
  ${clipp_SOURCE_DIR}/include
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ftxui::component ftxui::screen ftxui::dom tiny-process-library xlnt sqlite3_lib z ${Boost_LIBRARIES})

