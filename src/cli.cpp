#include "cli.h"

#include <algorithm>
#include <cstdlib>
#include <thread>

namespace mediagrep {

std::size_t defaultJobs() {
    const unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) return 1;
    return std::min<std::size_t>(2, hw);
}

std::string usageText() {
    return
        "usage: mediagrep [OPTIONS] -e EXPR [-e EXPR ...] PATH...\n"
        "       mediagrep [OPTIONS] PATTERN PATH...\n"
        "       mediagrep --fields FILE\n"
        "\n"
        "Search the metadata of media files. PATTERN is a regex matched against\n"
        "every field of every track. EXPR is an expression such as\n"
        "  'Video/Height < 1000 and not Audio/Language = ja'\n"
        "  'Duration > 1h or Format = Matroska'\n"
        "  '(Title ~ \"engine\" or Publisher ~ \"engine\") and \"60 minutes\"'\n"
        "Fields are [Track[N]/]Name; tracks are General, Video, Audio, Text, Image,\n"
        "Menu, Other. Operators: = != ~ !~ < <= > >=. A bare field tests existence.\n"
        "Directories are searched only with -r or -R, as with grep.\n"
        "\n"
        "  -e EXPR          expression; repeatable, ORed together\n"
        "  -i               case-insensitive matching\n"
        "  -v               select files that do not match\n"
        "  -l               print only matching paths\n"
        "  -L               print only non-matching paths\n"
        "  -c               print a count of matching files per PATH\n"
        "  -0               end records with NUL instead of newline\n"
        "  -r               recurse into directories, not following symlinks\n"
        "  -R               recurse and follow symlinks\n"
        "  --include GLOB   only files whose name matches; repeatable\n"
        "  --exclude GLOB   skip files whose name matches; repeatable\n"
        "  -j N             worker threads (default: min(2, CPUs))\n"
        "  --parse-speed F  MediaInfoLib ParseSpeed, 0 (headers only) to 1\n"
        "  --fields FILE    list every field of one file and exit\n"
        "  -q               no output, exit status only\n"
        "  -s               suppress warnings about unreadable files\n"
        "  --progress       show progress on stderr; the default when stderr is a\n"
        "                   terminal, as a plain line every 10 s when it is not\n"
        "  --no-progress    never show progress\n"
        "  -h, --help       this help\n"
        "  --version        version of mediagrep and MediaInfoLib\n"
        "\n"
        "Exit status is 0 if any file was selected, 1 if none, 2 on error.\n";
}

namespace {

bool parseJobs(const std::string& text, std::size_t& out) {
    if (text.empty() || !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return false;
    const unsigned long v = std::strtoul(text.c_str(), nullptr, 10);
    if (v == 0) return false;
    out = static_cast<std::size_t>(v);
    return true;
}

bool parseSpeed(const std::string& text, double& out) {
    if (text.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size()) return false;
    if (!(v >= 0.0 && v <= 1.0)) return false;  // also rejects NaN
    out = v;
    return true;
}

}  // namespace

std::optional<Options> parseArgs(const std::vector<std::string>& args, std::string& error) {
    Options o;
    std::vector<std::string> positionals;
    bool endOfOptions = false;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (endOfOptions || arg.size() < 2 || arg[0] != '-') {
            positionals.push_back(arg);
            continue;
        }
        if (arg == "--") { endOfOptions = true; continue; }

        if (arg.compare(0, 2, "--") == 0) {
            const std::size_t eq = arg.find('=');
            const std::string name = arg.substr(0, eq);
            std::optional<std::string> inlineValue;
            if (eq != std::string::npos) inlineValue = arg.substr(eq + 1);

            auto value = [&]() -> std::optional<std::string> {
                if (inlineValue) return inlineValue;
                if (i + 1 < args.size()) return args[++i];
                error = "option " + name + " requires a value";
                return std::nullopt;
            };

            if (name == "--help") { o.help = true; }
            else if (name == "--version") { o.version = true; }
            else if (name == "--progress") { o.progress = Options::Progress::On; }
            else if (name == "--no-progress") { o.progress = Options::Progress::Off; }
            else if (name == "--include") { auto v = value(); if (!v) return std::nullopt; o.include.push_back(*v); }
            else if (name == "--exclude") { auto v = value(); if (!v) return std::nullopt; o.exclude.push_back(*v); }
            else if (name == "--fields") { auto v = value(); if (!v) return std::nullopt; o.fieldsFile = *v; }
            else if (name == "--parse-speed") {
                auto v = value();
                if (!v) return std::nullopt;
                if (!parseSpeed(*v, o.parseSpeed)) {
                    error = "--parse-speed must be a number from 0 to 1, got '" + *v + "'";
                    return std::nullopt;
                }
            }
            else { error = "unknown option " + name; return std::nullopt; }
            continue;
        }

        // Cluster of short options, e.g. -ilR0. -e and -j take the rest of
        // the cluster or the next argument as their value.
        for (std::size_t j = 1; j < arg.size(); ++j) {
            const char c = arg[j];
            auto value = [&]() -> std::optional<std::string> {
                if (j + 1 < arg.size()) { std::string v = arg.substr(j + 1); j = arg.size(); return v; }
                if (i + 1 < args.size()) { j = arg.size(); return args[++i]; }
                error = std::string("option -") + c + " requires a value";
                return std::nullopt;
            };
            switch (c) {
                case 'i': o.ignoreCase = true; break;
                case 'v': o.invert = true; break;
                case 'l': o.output = Options::Output::FilesWithMatches; break;
                case 'L': o.output = Options::Output::FilesWithoutMatch; break;
                case 'c': o.output = Options::Output::Count; break;
                case '0': o.nullSeparated = true; break;
                case 'r': o.recurse = true; o.followSymlinks = false; break;
                case 'R': o.recurse = true; o.followSymlinks = true; break;
                case 'q': o.quiet = true; break;
                case 's': o.noMessages = true; break;
                case 'h': o.help = true; break;
                case 'e': { auto v = value(); if (!v) return std::nullopt; o.expressions.push_back(*v); break; }
                case 'j': {
                    auto v = value();
                    if (!v) return std::nullopt;
                    if (!parseJobs(*v, o.jobs)) {
                        error = "-j must be a positive number, got '" + *v + "'";
                        return std::nullopt;
                    }
                    break;
                }
                default:
                    error = std::string("unknown option -") + c;
                    return std::nullopt;
            }
        }
    }

    if (!o.help && !o.version && !o.fieldsFile) {
        if (o.expressions.empty()) {
            if (positionals.empty()) {
                error = "no expression or pattern given";
                return std::nullopt;
            }
            o.pattern = positionals.front();
            positionals.erase(positionals.begin());
        }
        o.paths = positionals;
        if (o.paths.empty()) {
            error = "no PATH given";
            return std::nullopt;
        }
    }
    return o;
}

bool isSelected(const Options& options, const Match& match) {
    const bool selected = match.matched != options.invert;
    return options.output == Options::Output::FilesWithoutMatch ? !selected : selected;
}

std::string formatFile(const Options& options, const std::string& path, const Match& match) {
    if (options.quiet) return "";
    const char sep = options.nullSeparated ? '\0' : '\n';
    const bool selected = isSelected(options, match);

    switch (options.output) {
        case Options::Output::Fields: {
            if (!selected) return "";
            if (options.invert || match.fields.empty()) return path + sep;
            std::string out;
            for (const auto& f : match.fields)
                out += path + ": " + f.location() + ": " + f.value + sep;
            return out;
        }
        case Options::Output::FilesWithMatches:
        case Options::Output::FilesWithoutMatch:
            return selected ? path + sep : "";
        case Options::Output::Count:
            return "";
    }
    return "";
}

}  // namespace mediagrep
