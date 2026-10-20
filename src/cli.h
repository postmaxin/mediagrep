// Command-line options and output formatting.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "eval.h"

namespace mediagrep {

struct Options {
    enum class Output { Fields, FilesWithMatches, FilesWithoutMatch, Count };
    enum class Progress { Auto, On, Off };

    std::vector<std::string> expressions;   // -e, ORed together
    std::optional<std::string> pattern;     // first positional when no -e
    std::vector<std::string> paths;         // at least one is required
    bool ignoreCase = false;                // -i
    bool invert = false;                    // -v
    Output output = Output::Fields;         // -l, -L, -c
    bool nullSeparated = false;             // -0
    bool recurse = false;                   // -r or -R
    bool followSymlinks = false;            // -R
    std::vector<std::string> include;       // --include
    std::vector<std::string> exclude;       // --exclude
    std::size_t jobs = 0;                   // -j; 0 means default
    double parseSpeed = 0.0;                // --parse-speed
    std::optional<std::string> fieldsFile;  // --fields FILE
    bool quiet = false;                     // -q
    Progress progress = Progress::Auto;     // --progress, --no-progress
    bool noMessages = false;                // -s
    bool help = false;                      // --help, -h
    bool version = false;                   // --version
};

// Default worker count: min(2, hardware threads). Disk and network seeks
// are the bottleneck, so more workers mostly add contention.
std::size_t defaultJobs();

// Parses argv[1..]. On failure returns nullopt and sets `error`.
std::optional<Options> parseArgs(const std::vector<std::string>& args, std::string& error);

std::string usageText();

// Whether a media file counts as selected, for the exit status, -c, -q and
// the progress count as well as for the output: -v inverts the match first,
// then -L takes the complement of that. Not meaningful with --fields.
bool isSelected(const Options& options, const Match& match);

// The output for one processed file, including the trailing record
// separators. Empty when nothing should be printed for it. `path` is
// already in display form. Count mode is handled by the caller.
std::string formatFile(const Options& options, const std::string& path, const Match& match);

}  // namespace mediagrep
