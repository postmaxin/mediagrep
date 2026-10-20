// libFuzzer target for the file-name recognizer. The input is split at the
// first NUL into the file name and its parent directory's name.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "path_track.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const char* text = reinterpret_cast<const char*>(data);
    const void* nul = size ? std::memchr(text, '\0', size) : nullptr;
    const size_t split = nul ? static_cast<size_t>(static_cast<const char*>(nul) - text) : size;
    const std::string name(text, split);
    const std::string directory = split < size ? std::string(text + split + 1, size - split - 1)
                                               : std::string();
    (void)mediagrep::recognizePath(name, directory);
    return 0;
}
