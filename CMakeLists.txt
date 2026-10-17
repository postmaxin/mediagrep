cmake_minimum_required(VERSION 3.16)
project(mediagrep VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MEDIAGREP_BUILD_TESTS "Build the test suite" ON)
option(MEDIAGREP_LINK_STATIC
       "Link MediaInfoLib at build time instead of loading it at runtime" OFF)

# ---------------------------------------------------------------------------
# Core library: everything except main(), so tests can link it.
# ---------------------------------------------------------------------------
add_library(mediagrep_core STATIC
    src/eval.cpp
    src/expr.cpp
    src/mediainfo_source.cpp
    src/parser.cpp
    src/units.cpp
    src/utf.cpp
)
target_include_directories(mediagrep_core
    PUBLIC src
    PRIVATE third_party/MediaInfoDLL
)
# Use MediaInfoLib's wide-character API everywhere; the 8-bit one is deprecated.
target_compile_definitions(mediagrep_core PRIVATE UNICODE _UNICODE)

if(MSVC)
    target_compile_options(mediagrep_core PUBLIC /W4 /utf-8 /EHsc)
else()
    target_compile_options(mediagrep_core PUBLIC -Wall -Wextra -Wpedantic)
endif()

if(MEDIAGREP_LINK_STATIC)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MEDIAINFO REQUIRED IMPORTED_TARGET libmediainfo)
    target_compile_definitions(mediagrep_core PRIVATE MEDIAGREP_LINK_STATIC)
    target_link_libraries(mediagrep_core PUBLIC PkgConfig::MEDIAINFO)
else()
    # MediaInfoDLL.h loads the library with dlopen/LoadLibrary at runtime.
    target_link_libraries(mediagrep_core PUBLIC ${CMAKE_DL_LIBS})
endif()

find_package(Threads REQUIRED)
target_link_libraries(mediagrep_core PUBLIC Threads::Threads)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(MEDIAGREP_BUILD_TESTS)
    enable_testing()
    add_executable(mediagrep_tests
        tests/main.cpp
        tests/test_eval.cpp
        tests/test_mediainfo.cpp
        tests/test_parser.cpp
        tests/test_units.cpp
        tests/test_utf.cpp
    )
    target_include_directories(mediagrep_tests PRIVATE third_party/doctest)
    target_compile_definitions(mediagrep_tests PRIVATE
        MEDIAGREP_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    target_link_libraries(mediagrep_tests PRIVATE mediagrep_core)
    add_test(NAME unit COMMAND mediagrep_tests)
endif()
