# libFuzzer targets. Configure with -DMEDIAGREP_BUILD_FUZZERS=ON and clang;
# the top-level CMakeLists.txt instruments mediagrep_core when it is on.
foreach(target fuzz_parser fuzz_units fuzz_path)
    add_executable(${target} ${target}.cpp)
    target_link_libraries(${target} PRIVATE mediagrep_core)
    target_compile_options(${target} PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(${target} PRIVATE -fsanitize=fuzzer,address,undefined)
endforeach()
