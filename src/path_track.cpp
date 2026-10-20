#include "path_track.h"

#include <cctype>
#include <cstdlib>
#include <optional>
#include <regex>
#include <utility>

namespace mediagrep {

namespace {

bool isAlnum(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }

// True if the characters just outside [begin, end) are not alphanumeric,
// so "1080p" and the inside of a yt-dlp id never count as numbers.
bool delimited(const std::string& s, std::size_t begin, std::size_t end) {
    return (begin == 0 || !isAlnum(s[begin - 1])) && (end == s.size() || !isAlnum(s[end]));
}

bool plausibleYear(int y) { return y >= 1900 && y <= 2099; }
bool plausibleDate(int y, int m, int d) { return plausibleYear(y) && m >= 1 && m <= 12 && d >= 1 && d <= 31; }

std::string pad2(int n) { return (n < 10 ? "0" : "") + std::to_string(n); }

// Visits each delimited match of `re` in `s`, stopping when fn returns true.
template <class Fn>
void forEachDelimitedMatch(const std::string& s, const std::regex& re, Fn fn) {
    for (std::sregex_iterator it(s.begin(), s.end(), re), end; it != end; ++it) {
        const std::size_t b = static_cast<std::size_t>(it->position(0));
        const std::size_t e = b + static_cast<std::size_t>(it->length(0));
        if (delimited(s, b, e) && fn(*it)) return;
    }
}

std::optional<std::string> findDate(const std::string& name) {
    std::optional<std::string> found;

    static const std::regex full(R"((\d{4})[-_.](\d{2})[-_.](\d{2}))");
    forEachDelimitedMatch(name, full, [&](const std::smatch& m) {
        const int y = std::atoi(m[1].str().c_str()), mo = std::atoi(m[2].str().c_str()),
                  d = std::atoi(m[3].str().c_str());
        if (!plausibleDate(y, mo, d)) return false;
        found = std::to_string(y) + "-" + pad2(mo) + "-" + pad2(d);
        return true;
    });
    if (found) return found;

    static const std::regex compact(R"(\d{8})");
    forEachDelimitedMatch(name, compact, [&](const std::smatch& m) {
        const std::string t = m[0].str();
        const int y = std::atoi(t.substr(0, 4).c_str()), mo = std::atoi(t.substr(4, 2).c_str()),
                  d = std::atoi(t.substr(6, 2).c_str());
        if (!plausibleDate(y, mo, d)) return false;
        found = t.substr(0, 4) + "-" + t.substr(4, 2) + "-" + t.substr(6, 2);
        return true;
    });
    if (found) return found;

    static const std::regex paren(R"(\((\d{4})\))");
    for (std::sregex_iterator it(name.begin(), name.end(), paren), end; it != end; ++it) {
        if (plausibleYear(std::atoi((*it)[1].str().c_str()))) return (*it)[1].str();
    }

    static const std::regex year(R"(\d{4})");
    forEachDelimitedMatch(name, year, [&](const std::smatch& m) {
        if (!plausibleYear(std::atoi(m[0].str().c_str()))) return false;
        found = m[0].str();
        return true;
    });
    return found;
}

struct SeasonEpisode {
    std::optional<int> season;
    std::optional<int> episode;
};

SeasonEpisode findSeasonEpisode(const std::string& name, const std::string& directory) {
    SeasonEpisode se;
    static const std::regex sxe(R"([Ss](\d{1,2})[Ee](\d{1,3}))");
    forEachDelimitedMatch(name, sxe, [&](const std::smatch& m) {
        se.season = std::atoi(m[1].str().c_str());
        se.episode = std::atoi(m[2].str().c_str());
        return true;
    });
    if (!se.season) {
        static const std::regex nxn(R"((\d{1,2})x(\d{2,3}))");
        forEachDelimitedMatch(name, nxn, [&](const std::smatch& m) {
            se.season = std::atoi(m[1].str().c_str());
            se.episode = std::atoi(m[2].str().c_str());
            return true;
        });
    }
    if (!se.season) {
        static const std::regex dir(R"(^(?:[Ss]eason[ _.-]?|[Ss])(\d{1,2})$)");
        std::smatch m;
        if (std::regex_match(directory, m, dir)) se.season = std::atoi(m[1].str().c_str());
    }
    return se;
}

// yt-dlp appends " [id]" just before the extension(s), so the id is the
// bracketed 11-character word at the end of the name, optionally followed by
// extensions such as ".f137.mp4" or ".en.vtt". A bracketed word elsewhere is
// part of the title. `range` receives the "[id]" token, brackets included.
std::optional<std::string> findId(const std::string& name,
                                  std::pair<std::size_t, std::size_t>* range = nullptr) {
    static const std::regex id(R"(\[([A-Za-z0-9_-]{11})\]((?:\.[A-Za-z0-9]+)*)$)");
    std::smatch m;
    if (!std::regex_search(name, m, id)) return std::nullopt;
    if (range) *range = {static_cast<std::size_t>(m.position(0)), 13};
    return m[1].str();
}

}  // namespace

std::vector<FieldValue> recognizePath(const std::string& name, const std::string& directory) {
    std::vector<FieldValue> out;
    // The id is random text; mask it so a year or SxxEyy inside it, set off
    // by '_' or '-', is not mistaken for a date or an episode.
    std::string masked = name;
    std::pair<std::size_t, std::size_t> idRange;
    const auto id = findId(name, &idRange);
    if (id) masked.replace(idRange.first, idRange.second, idRange.second, ' ');
    if (const auto when = findDate(masked)) out.push_back({"When", *when, ValueType::Text, ""});
    const SeasonEpisode se = findSeasonEpisode(masked, directory);
    if (se.season) out.push_back({"Season", std::to_string(*se.season), ValueType::Integer, ""});
    if (se.episode) out.push_back({"Episode", std::to_string(*se.episode), ValueType::Integer, ""});
    if (id) out.push_back({"Id", *id, ValueType::Text, ""});
    return out;
}

}  // namespace mediagrep
