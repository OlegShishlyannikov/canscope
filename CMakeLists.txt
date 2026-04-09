cmake_minimum_required(VERSION 3.13)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MESSAGE_LOG_LEVEL TRACE)
set(CMAKE_VERBOSE_MAKEFILE OFF)
set(CMAKE_CXX_COMPILER /usr/bin/clang++ CACHE INTERNAL "cxx compiler")
option(BUILD_SHARED_LIBS "Build using shared libraries" OFF)

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

FetchContent_Declare(
  tpl
  GIT_REPOSITORY https://gitlab.com/eidheim/tiny-process-library.git
  GIT_TAG        8bbb5a211c5c9df8ee69301da9d22fb977b27dc1
  GIT_PROGRESS   TRUE
)

FetchContent_MakeAvailable(tpl)
FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG        v6.0.0
  GIT_SHALLOW    TRUE
  GIT_PROGRESS   TRUE
  PATCH_COMMAND  git apply --check ${CMAKE_SOURCE_DIR}/thirdparty/ftxui-empty-container.patch 2>/dev/null && git apply ${CMAKE_SOURCE_DIR}/thirdparty/ftxui-empty-container.patch || true COMMAND git apply --check ${CMAKE_SOURCE_DIR}/thirdparty/ftxui-window.patch 2>/dev/null && git apply ${CMAKE_SOURCE_DIR}/thirdparty/ftxui-window.patch || true
)

FetchContent_MakeAvailable(ftxui)
FetchContent_Declare(
  sqlite_modern
  GIT_REPOSITORY https://github.com/SqliteModernCpp/sqlite_modern_cpp
  GIT_TAG        6e3009973025e0016d5573529067714201338c80
  GIT_PROGRESS   TRUE
)

FetchContent_GetProperties(sqlite_modern)
if(NOT sqlite_modern_POPULATED)
  FetchContent_Populate(sqlite_modern)
endif()


option(STATIC "Set to ON to build xlnt as a static library instead of a shared library" OFF)
FetchContent_Declare(
  xlnt
  GIT_REPOSITORY https://github.com/xlnt-community/xlnt.git
  GIT_TAG        e165887739147027e7fbab918280b88f9efa5ffb
  GIT_PROGRESS   TRUE
)

FetchContent_MakeAvailable(xlnt)

# Suppress warnings in xlnt
if(TARGET xlnt)
  target_compile_options(xlnt PRIVATE
    -Wno-unsafe-buffer-usage-in-libc-call
    -Wno-unsafe-buffer-usage
    -Wno-undefined-reinterpret-cast
    -Wno-extra-semi-stmt
    -Wno-sign-conversion
    -Wno-old-style-cast
    -Wno-switch-default
    -Wno-nrvo
    -Wno-reserved-identifier
    -Wno-unused-but-set-variable
    -Wno-missing-prototypes
    -Wno-character-conversion
    -Wno-implicit-int-float-conversion
    -Wno-float-equal
    -Wno-global-constructors
    -Wno-unique-object-duplication
  )
endif()

set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_DOC OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt
  GIT_TAG        11.1.4
  GIT_SHALLOW    TRUE
  GIT_PROGRESS   TRUE
)

FetchContent_MakeAvailable(fmt)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json
  GIT_TAG        v3.12.0
  GIT_SHALLOW    TRUE
  GIT_PROGRESS   TRUE
)

FetchContent_MakeAvailable(json)
set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog
  GIT_TAG        v1.15.3
  GIT_SHALLOW    TRUE
  GIT_PROGRESS   TRUE
)

FetchContent_MakeAvailable(spdlog)

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
)

target_link_directories(${CMAKE_PROJECT_NAME}
  PRIVATE
  ${ftxui_BINARY_DIR}
  ${tpl_BINARY_DIR}
  ${xlnt_BINARY_DIR}
  ${fmt_BINARY_DIR}
  ${json_BINARY_DIR}
  #{sqlite_modern_BINARY_DIR}
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ftxui-component ftxui-screen ftxui-dom tiny-process-library xlnt sqlite3 spdlog::spdlog systemd z ${Boost_LIBRARIES})
# target_link_libraries(${CMAKE_PROJECT_NAME} PUBLIC sqlite3 ${Boost_LIBRARIES})
