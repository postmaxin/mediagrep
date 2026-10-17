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
    src/cli.cpp
    src/eval.cpp
    src/expr.cpp
    src/mediainfo_source.cpp
    src/parser.cpp
    src/units.cpp
    src/utf.cpp
    src/walker.cpp
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
    # Only the link line is taken from pkg-config. The headers come from the
    # vendored MediaInfoDLL_Static.h, and some distributions' libmediainfo.pc
    # lists include directories (glib) that the dev package does not provide.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MEDIAINFO REQUIRED libmediainfo)
    target_compile_definitions(mediagrep_core PRIVATE MEDIAGREP_LINK_STATIC)
    target_link_directories(mediagrep_core PUBLIC ${MEDIAINFO_LIBRARY_DIRS})
    target_link_libraries(mediagrep_core PUBLIC ${MEDIAINFO_LIBRARIES})
else()
    # MediaInfoDLL.h loads the library with dlopen/LoadLibrary at runtime,
    # by soname (libmediainfo.so.0, libmediainfo.0.dylib, MediaInfo.dll).
    # MEDIAGREP_MEDIAINFO_LIBRARY overrides that with a path, for installs
    # outside the loader's default search path. On macOS, Homebrew's
    # /opt/homebrew/lib is such a place, so look there automatically.
    set(MEDIAGREP_MEDIAINFO_LIBRARY "" CACHE STRING
        "Path or soname of the MediaInfoLib shared library to load at runtime")
    if(APPLE AND NOT MEDIAGREP_MEDIAINFO_LIBRARY)
        find_library(_mediagrep_mi_lib NAMES mediainfo.0 mediainfo
                     PATHS /opt/homebrew/lib /usr/local/lib NO_DEFAULT_PATH)
        if(_mediagrep_mi_lib)
            set(MEDIAGREP_MEDIAINFO_LIBRARY "${_mediagrep_mi_lib}")
        endif()
    endif()
    if(MEDIAGREP_MEDIAINFO_LIBRARY)
        message(STATUS "MediaInfoLib will be loaded from ${MEDIAGREP_MEDIAINFO_LIBRARY}")
        if(WIN32)
            target_compile_definitions(mediagrep_core PRIVATE
                "MEDIAINFODLL_NAME=L\"${MEDIAGREP_MEDIAINFO_LIBRARY}\"")
        else()
            target_compile_definitions(mediagrep_core PRIVATE
                "MEDIAINFODLL_NAME=\"${MEDIAGREP_MEDIAINFO_LIBRARY}\"")
        endif()
    endif()
    target_link_libraries(mediagrep_core PUBLIC ${CMAKE_DL_LIBS})
endif()

find_package(Threads REQUIRED)
target_link_libraries(mediagrep_core PUBLIC Threads::Threads)

# ---------------------------------------------------------------------------
# The command-line tool
# ---------------------------------------------------------------------------
add_executable(mediagrep src/main.cpp)
target_link_libraries(mediagrep PRIVATE mediagrep_core)
target_compile_definitions(mediagrep PRIVATE MEDIAGREP_VERSION="${PROJECT_VERSION}")
install(TARGETS mediagrep RUNTIME DESTINATION bin)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(MEDIAGREP_BUILD_TESTS)
    enable_testing()
    add_executable(mediagrep_tests
        tests/main.cpp
        tests/test_cli.cpp
        tests/test_eval.cpp
        tests/test_mediainfo.cpp
        tests/test_options.cpp
        tests/test_parser.cpp
        tests/test_pool.cpp
        tests/test_units.cpp
        tests/test_utf.cpp
        tests/test_walker.cpp
    )
    target_include_directories(mediagrep_tests PRIVATE third_party/doctest)
    target_compile_definitions(mediagrep_tests PRIVATE
        MEDIAGREP_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
        MEDIAGREP_BINARY="$<TARGET_FILE:mediagrep>")
    target_link_libraries(mediagrep_tests PRIVATE mediagrep_core)
    add_dependencies(mediagrep_tests mediagrep)
    add_test(NAME unit COMMAND mediagrep_tests)
endif()
