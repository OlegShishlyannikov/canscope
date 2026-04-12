include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

FetchContent_Declare(
  tpl
  GIT_REPOSITORY git@gitlab.com:eidheim/tiny-process-library.git
  GIT_TAG        8bbb5a211c5c9df8ee69301da9d22fb977b27dc1
  GIT_PROGRESS   TRUE
)

FetchContent_MakeAvailable(tpl)

FetchContent_Declare(
  ftxui
  GIT_REPOSITORY git@github.com:ArthurSonzogni/FTXUI.git
  GIT_TAG        v6.0.0
  GIT_SHALLOW    TRUE
  GIT_PROGRESS   TRUE
  PATCH_COMMAND  git apply --check ${CMAKE_SOURCE_DIR}/cmake/patches/ftxui-empty-container.patch 2>/dev/null && git apply ${CMAKE_SOURCE_DIR}/cmake/patches/ftxui-empty-container.patch || true COMMAND git apply --check ${CMAKE_SOURCE_DIR}/cmake/patches/ftxui-window.patch 2>/dev/null && git apply ${CMAKE_SOURCE_DIR}/cmake/patches/ftxui-window.patch || true
)

FetchContent_MakeAvailable(ftxui)

FetchContent_Declare(
  sqlite_modern
  GIT_REPOSITORY git@github.com:SqliteModernCpp/sqlite_modern_cpp
  GIT_TAG        6e3009973025e0016d5573529067714201338c80
  GIT_PROGRESS   TRUE
)

FetchContent_GetProperties(sqlite_modern)
if(NOT sqlite_modern_POPULATED)
  FetchContent_Populate(sqlite_modern)
endif()

FetchContent_Declare(
  sqlite3
  URL      https://www.sqlite.org/2024/sqlite-amalgamation-3470200.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(sqlite3)
add_library(sqlite3_lib STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3_lib PUBLIC ${sqlite3_SOURCE_DIR})
target_compile_definitions(sqlite3_lib PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION)

option(STATIC "Set to ON to build xlnt as a static library instead of a shared library" OFF)
FetchContent_Declare(
  xlnt
  GIT_REPOSITORY git@github.com:xlnt-community/xlnt.git
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
  GIT_REPOSITORY git@github.com:fmtlib/fmt
  GIT_TAG        11.1.4
  GIT_SHALLOW    TRUE
  GIT_PROGRESS   TRUE
)

FetchContent_MakeAvailable(fmt)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  json
  GIT_REPOSITORY git@github.com:nlohmann/json
  GIT_TAG        v3.12.0
  GIT_SHALLOW    TRUE
  GIT_PROGRESS   TRUE
)

FetchContent_MakeAvailable(json)

FetchContent_Declare(
  clipp
  GIT_REPOSITORY git@github.com:muellan/clipp.git
  GIT_TAG        v1.2.3
  GIT_SHALLOW    TRUE
  GIT_PROGRESS   TRUE
)

FetchContent_GetProperties(clipp)
if(NOT clipp_POPULATED)
  FetchContent_Populate(clipp)
endif()
