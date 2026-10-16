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
    src/parser.cpp
    src/units.cpp
)
target_include_directories(mediagrep_core PUBLIC src)

if(MSVC)
    target_compile_options(mediagrep_core PUBLIC /W4 /utf-8 /EHsc)
else()
    target_compile_options(mediagrep_core PUBLIC -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(MEDIAGREP_BUILD_TESTS)
    enable_testing()
    add_executable(mediagrep_tests
        tests/main.cpp
        tests/test_eval.cpp
        tests/test_parser.cpp
        tests/test_units.cpp
    )
    target_include_directories(mediagrep_tests PRIVATE third_party/doctest)
    target_link_libraries(mediagrep_tests PRIVATE mediagrep_core)
    add_test(NAME unit COMMAND mediagrep_tests)
endif()
