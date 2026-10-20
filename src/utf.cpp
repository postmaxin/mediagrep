#include "utf.h"

namespace mediagrep {

namespace {

constexpr char32_t kReplacement = 0xFFFD;

void appendWide(std::wstring& out, char32_t cp) {
    if constexpr (sizeof(wchar_t) == 2) {
        if (cp > 0xFFFF) {
            cp -= 0x10000;
            out += static_cast<wchar_t>(0xD800 + (cp >> 10));
            out += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
            return;
        }
    }
    out += static_cast<wchar_t>(cp);
}

void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

}  // namespace

std::wstring widen(std::string_view s) {
    std::wstring out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char lead = static_cast<unsigned char>(s[i]);
        char32_t cp = 0;
        std::size_t len = 0;
        if (lead < 0x80) { cp = lead; len = 1; }
        else if ((lead & 0xE0) == 0xC0) { cp = lead & 0x1F; len = 2; }
        else if ((lead & 0xF0) == 0xE0) { cp = lead & 0x0F; len = 3; }
        else if ((lead & 0xF8) == 0xF0) { cp = lead & 0x07; len = 4; }
        else { appendWide(out, kReplacement); ++i; continue; }

        if (i + len > s.size()) { appendWide(out, kReplacement); break; }

        bool valid = true;
        for (std::size_t k = 1; k < len; ++k) {
            const unsigned char c = static_cast<unsigned char>(s[i + k]);
            if ((c & 0xC0) != 0x80) { valid = false; break; }
            cp = (cp << 6) | (c & 0x3F);
        }
        if (!valid) { appendWide(out, kReplacement); ++i; continue; }

        const bool overlong = (len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) ||
                              (len == 4 && cp < 0x10000);
        if (overlong || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = kReplacement;
        appendWide(out, cp);
        i += len;
    }
    return out;
}

std::string narrow(std::wstring_view w) {
    std::string out;
    out.reserve(w.size());
    for (std::size_t i = 0; i < w.size(); ++i) {
        char32_t cp = static_cast<char32_t>(static_cast<unsigned long>(w[i]) & 0xFFFFFFFFul);
        if constexpr (sizeof(wchar_t) == 2) {
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                if (i + 1 < w.size()) {
                    const char32_t low = static_cast<char32_t>(w[i + 1]) & 0xFFFF;
                    if (low >= 0xDC00 && low <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        ++i;
                    } else {
                        cp = kReplacement;
                    }
                } else {
                    cp = kReplacement;
                }
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                cp = kReplacement;
            }
        } else if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            cp = kReplacement;
        }
        appendUtf8(out, cp);
    }
    return out;
}

}  // namespace mediagrep
