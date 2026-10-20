// libFuzzer target for the -e expression parser. Any input must either
// parse or throw ParseError; anything else (another exception, a crash, a
// sanitizer report) is a bug. A parsed tree is rendered too, since error
// messages and tests walk it.
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "parser.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    try {
        auto expr = mediagrep::parse(
            std::string_view(reinterpret_cast<const char*>(data), size));
        if (expr) (void)mediagrep::toString(*expr);
    } catch (const mediagrep::ParseError&) {
    }
    return 0;
}
