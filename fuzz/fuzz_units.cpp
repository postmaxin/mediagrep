// libFuzzer target for the operand parsers: numbers with units and dates.
// The first input byte picks the measure string and the entry point; the
// rest is the text. All of them return nullopt on bad input and must never
// throw or misbehave.
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "units.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0) return 0;
    static const std::string_view kMeasures[] = {" ms", " byte", " bps", "", " Hz", " pixel"};
    const uint8_t selector = data[0];
    const std::string_view text(reinterpret_cast<const char*>(data) + 1, size - 1);
    const size_t nMeasures = sizeof(kMeasures) / sizeof(kMeasures[0]);

    switch (selector % 3) {
        case 0:
            (void)mediagrep::parseNumber(text, kMeasures[(selector / 3) % nMeasures]);
            break;
        case 1:
            (void)mediagrep::normalizeDateValue(text);
            break;
        default:
            (void)mediagrep::normalizeDateOperand(text);
            break;
    }
    return 0;
}
