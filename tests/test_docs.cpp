// The man page must document every option the help text lists.
#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#include "cli.h"
#include "doctest.h"

using namespace mediagrep;

namespace {

std::string readFile(const char* path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

// "-e" -> "\-e", "--no-progress" -> "\-\-no\-progress", as roff spells them.
std::string roff(const std::string& option) {
    std::string out;
    for (char c : option) {
        if (c == '-') out += '\\';
        out += c;
    }
    return out;
}

}  // namespace

TEST_CASE("the man page documents every option in the help text, and the version") {
    const std::string man = readFile(MEDIAGREP_MANPAGE);
    REQUIRE_FALSE(man.empty());
    CHECK(man.find("mediagrep " MEDIAGREP_VERSION_FOR_TESTS) != std::string::npos);

    // Option lines look like "  -e EXPR          ..." or "  -h, --help       ...":
    // the option column is everything before the description.
    std::istringstream usage(usageText());
    const std::regex option(R"(--?[A-Za-z0-9][a-z-]*)");
    int seen = 0;
    for (std::string line; std::getline(usage, line);) {
        if (line.compare(0, 3, "  -") != 0) continue;
        const std::string column = line.substr(0, std::min<std::size_t>(19, line.size()));
        for (auto it = std::sregex_iterator(column.begin(), column.end(), option);
             it != std::sregex_iterator(); ++it) {
            const std::string name = it->str();
            CAPTURE(name);
            CHECK(man.find(roff(name)) != std::string::npos);
            ++seen;
        }
    }
    CHECK(seen >= 20);
}
