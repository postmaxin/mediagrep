// UTF-8 <-> wchar_t conversion for the MediaInfoLib boundary.
//
// MediaInfoLib's recommended API is the wide one. wchar_t is UTF-16 on
// Windows and UTF-32 elsewhere; both are handled here. Everything inside
// mediagrep is UTF-8.
#pragma once

#include <string>
#include <string_view>

namespace mediagrep {

// Invalid UTF-8 sequences become U+FFFD.
std::wstring widen(std::string_view utf8);

// Unpaired surrogates (UTF-16) or out-of-range values become U+FFFD.
std::string narrow(std::wstring_view wide);

}  // namespace mediagrep
