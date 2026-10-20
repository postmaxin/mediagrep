cmake_minimum_required(VERSION 3.16)
project(mediagrep VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    # 64-bit off_t, struct stat and ino_t on 32-bit glibc and uClibc (Entware
    # on 32-bit ARM NAS boxes). Without it stat() fails with EOVERFLOW for a
    # file of 2 GiB or more, and readdir() for a large inode number. It does
    # nothing on 64-bit targets. Set here so it reaches every target.
    add_compile_definitions(_FILE_OFFSET_BITS=64)
endif()

if(MSVC)
    # Link the C and C++ runtimes statically (/MT, /MTd). The release zip does
    # not ship the Visual C++ redistributable, and without it a default /MD
    # mediagrep.exe will not start on a PC that lacks it. Needs policy CMP0091,
    # which cmake_minimum_required(3.16) sets to NEW.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(MEDIAGREP_BUILD_TESTS "Build the test suite" ON)
option(MEDIAGREP_LINK_STATIC
       "Link MediaInfoLib at build time instead of loading it at runtime" OFF)

option(MEDIAGREP_BUILD_FUZZERS
       "Build the libFuzzer targets in fuzz/ (needs clang)" OFF)
set(MEDIAGREP_SANITIZE "" CACHE STRING
    "Sanitizers to build with, e.g. address,undefined or thread (GCC or Clang)")

# ---------------------------------------------------------------------------
# Core library: everything except main(), so tests can link it.
# ---------------------------------------------------------------------------
add_library(mediagrep_core STATIC
    src/cli.cpp
    src/eval.cpp
    src/expr.cpp
    src/file_track.cpp
    src/fsys.cpp
    src/mediainfo_source.cpp
    src/parser.cpp
    src/path_track.cpp
    src/progress.cpp
    src/units.cpp
    src/term.cpp
    src/thread.cpp
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

if(MEDIAGREP_SANITIZE AND NOT MSVC)
    # PUBLIC so every target that links the core is instrumented too.
    target_compile_options(mediagrep_core PUBLIC
        -fsanitize=${MEDIAGREP_SANITIZE} -fno-omit-frame-pointer -g)
    target_link_options(mediagrep_core PUBLIC -fsanitize=${MEDIAGREP_SANITIZE})
endif()

if(MEDIAGREP_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "MEDIAGREP_BUILD_FUZZERS needs clang (libFuzzer)")
    endif()
    # Instrument the library for coverage and the sanitizers; the fuzz
    # targets add the libFuzzer runtime themselves.
    target_compile_options(mediagrep_core PUBLIC
        -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer -g)
    target_link_options(mediagrep_core PUBLIC -fsanitize=address,undefined)
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
        # The value becomes a C++ string literal, so backslashes (a Windows
        # path) and quotes in it must be escaped, or C:\temp\MediaInfo.dll
        # turns into a tab and a bad escape.
        string(REPLACE "\\" "\\\\" _mediagrep_mi_literal "${MEDIAGREP_MEDIAINFO_LIBRARY}")
        string(REPLACE "\"" "\\\"" _mediagrep_mi_literal "${_mediagrep_mi_literal}")
        if(WIN32)
            target_compile_definitions(mediagrep_core PRIVATE
                "MEDIAINFODLL_NAME=L\"${_mediagrep_mi_literal}\"")
        else()
            target_compile_definitions(mediagrep_core PRIVATE
                "MEDIAINFODLL_NAME=\"${_mediagrep_mi_literal}\"")
        endif()
    endif()
    target_link_libraries(mediagrep_core PUBLIC ${CMAKE_DL_LIBS})
    if(APPLE)
        # MediaInfoDLL_Load() looks for the dylib next to the running bundle
        # through CFBundle before falling back to the configured name.
        target_link_libraries(mediagrep_core PUBLIC "-framework CoreFoundation")
    endif()
endif()

find_package(Threads REQUIRED)
target_link_libraries(mediagrep_core PUBLIC Threads::Threads)

# ---------------------------------------------------------------------------
# The command-line tool
# ---------------------------------------------------------------------------
add_executable(mediagrep src/main.cpp)
target_link_libraries(mediagrep PRIVATE mediagrep_core)
target_compile_definitions(mediagrep PRIVATE MEDIAGREP_VERSION="${PROJECT_VERSION}")
include(GNUInstallDirs)
install(TARGETS mediagrep RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# The man page, with the version filled in.
configure_file(docs/mediagrep.1.in ${CMAKE_CURRENT_BINARY_DIR}/mediagrep.1 @ONLY)
if(NOT WIN32)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/mediagrep.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(MEDIAGREP_BUILD_TESTS)
    enable_testing()
    add_executable(mediagrep_tests
        tests/main.cpp
        tests/test_cli.cpp
        tests/test_docs.cpp
        tests/test_eval.cpp
        tests/test_file_track.cpp
        tests/test_fsys.cpp
        tests/test_mediainfo.cpp
        tests/test_module_search.cpp
        tests/test_options.cpp
        tests/test_parser.cpp
        tests/test_path.cpp
        tests/test_progress.cpp
        tests/test_regex_limits.cpp
        tests/test_pool.cpp
        tests/test_units.cpp
        tests/test_utf.cpp
        tests/test_walker.cpp
    )
    target_include_directories(mediagrep_tests PRIVATE third_party/doctest)
    target_compile_definitions(mediagrep_tests PRIVATE
        MEDIAGREP_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
        MEDIAGREP_UNICODE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/unicode"
        MEDIAGREP_MANPAGE="${CMAKE_CURRENT_BINARY_DIR}/mediagrep.1"
        MEDIAGREP_VERSION_FOR_TESTS="${PROJECT_VERSION}"
        MEDIAGREP_BINARY="$<TARGET_FILE:mediagrep>")
    if(MSVC)
        target_compile_definitions(mediagrep_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    target_link_libraries(mediagrep_tests PRIVATE mediagrep_core)
    add_dependencies(mediagrep_tests mediagrep)
    add_test(NAME unit COMMAND mediagrep_tests)
endif()

if(MEDIAGREP_BUILD_FUZZERS)
    add_subdirectory(fuzz)
endif()
