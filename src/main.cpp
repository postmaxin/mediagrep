// mediagrep: grep for media metadata.
#include <atomic>
#include <cstdio>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "cli.h"
#include "eval.h"
#include "file_track.h"
#include "mediainfo_source.h"
#include "parser.h"
#include "pool.h"
#include "progress.h"
#include "queue.h"
#include "term.h"
#include "utf.h"
#include "walker.h"

using namespace mediagrep;

namespace {

#ifndef MEDIAGREP_VERSION
#define MEDIAGREP_VERSION "0.0.0"
#endif

void warn(const std::string& message) {
    std::fputs(("mediagrep: " + message + "\n").c_str(), stderr);
}

void write(const std::string& s) {
    if (!s.empty()) std::fwrite(s.data(), 1, s.size(), stdout);
}

int dumpFields(const Options& options) {
    MediaInfoSource src(options.parseSpeed);
    if (!src.open(*options.fieldsFile)) {
        warn(*options.fieldsFile + ": cannot read");
        return 2;
    }
    if (!src.isMedia()) {
        warn(*options.fieldsFile + ": not recognized as a media file");
        return 1;
    }
    const char sep = options.nullSeparated ? '\0' : '\n';
    const FileTrackSource all(src, *options.fieldsFile);
    for (TrackKind kind : {TrackKind::General, TrackKind::Video, TrackKind::Audio, TrackKind::Text,
                           TrackKind::Image, TrackKind::Menu, TrackKind::Other, TrackKind::File,
                           TrackKind::Path}) {
        const std::size_t n = all.trackCount(kind);
        for (std::size_t i = 0; i < n; ++i) {
            all.forEachField(kind, i, [&](const FieldValue& fv) {
                MatchedField f{kind, i, n > 1, fv.name, fv.value};
                const std::string value = fv.type == ValueType::Binary ? "(binary)" : fv.value;
                write(f.location() + ": " + value + sep);
            });
        }
    }
    return 0;
}

// Parses every -e expression and the positional pattern into one Expr,
// ORing multiple expressions together. Returns null after printing an error.
ExprPtr buildExpression(const Options& options) {
    std::vector<ExprPtr> parts;
    for (const std::string& text : options.expressions) {
        try {
            parts.push_back(parse(text));
        } catch (const ParseError& e) {
            warn("in expression: " + std::string(e.what()));
            std::fputs(("  " + text + "\n  " + std::string(e.offset, ' ') + "^\n").c_str(), stderr);
            return nullptr;
        }
    }
    if (options.pattern) {
        auto node = std::make_unique<Expr>();
        node->kind = Expr::Kind::Pattern;
        node->value = *options.pattern;
        parts.push_back(std::move(node));
    }
    if (parts.size() == 1) return std::move(parts.front());
    auto node = std::make_unique<Expr>();
    node->kind = Expr::Kind::Or;
    node->children = std::move(parts);
    return node;
}

struct Job {
    std::string path;
    std::size_t root = 0;  // index into Options::paths, for -c
};

struct Result {
    std::string display;
    std::size_t root = 0;
    Match match;
    bool isMedia = false;
    bool error = false;
    std::string message;
};

// Paths the walker may discover ahead of the workers. Lets the progress
// total settle early on large trees while bounding memory.
constexpr std::size_t kMaxPendingPaths = 200000;

Progress::Mode progressMode(const Options& options) {
    switch (options.progress) {
        case Options::Progress::Off: return Progress::Mode::Off;
        case Options::Progress::On:
            return term::stderrIsTerminal() ? Progress::Mode::Line : Progress::Mode::Log;
        case Options::Progress::Auto: break;
    }
    return !options.quiet && term::stderrIsTerminal() ? Progress::Mode::Line : Progress::Mode::Off;
}

int run(const Options& options) {
    const ExprPtr expr = buildExpression(options);
    if (!expr) return 2;

    std::unique_ptr<Evaluator> evaluator;
    try {
        evaluator = std::make_unique<Evaluator>(*expr, EvalOptions{options.ignoreCase});
    } catch (const EvalError& e) {
        warn(e.what());
        return 2;
    }

    WalkOptions walkOptions;
    walkOptions.recurse = options.recurse;
    walkOptions.followSymlinks = options.followSymlinks;
    walkOptions.include = options.include;
    walkOptions.exclude = options.exclude;
    const std::size_t jobs = options.jobs ? options.jobs : defaultJobs();
    const char sep = options.nullSeparated ? '\0' : '\n';

    Progress progress(progressMode(options), term::stdoutIsTerminal());
    std::atomic<bool> anySelected{false};
    std::atomic<bool> anyError{false};
    std::vector<std::size_t> counts(options.paths.size(), 0);  // emit thread only
    std::set<std::string> warned;                               // emit thread only

    auto work = [&](const Job& job) -> Result {
        thread_local std::unique_ptr<MediaInfoSource> source;
        if (!source) source = std::make_unique<MediaInfoSource>(options.parseSpeed);
        progress.started(job.path);
        Result r;
        r.display = job.path;
        r.root = job.root;
        try {
            if (!source->open(job.path)) {
                r.error = true;
                r.message = "cannot read";
                return r;
            }
            if (!source->isMedia()) return r;
            r.isMedia = true;
            const FileTrackSource withFile(*source, job.path);
            r.match = evaluator->evaluate(withFile);
        } catch (const std::exception& e) {
            r.error = true;
            r.message = e.what();
        }
        return r;
    };

    auto emit = [&](Result&& r) {
        if (r.error) {
            anyError = true;
            progress.finished(false, true);
            if (!options.noMessages) progress.err("mediagrep: " + r.display + ": " + r.message + "\n");
            return;
        }
        if (!r.isMedia) {
            progress.finished(false, false);
            return;
        }
        for (const std::string& w : r.match.warnings)
            if (warned.insert(w).second) progress.err("mediagrep: warning: " + w + "\n");
        const bool selected = options.output == Options::Output::FilesWithoutMatch
                                  ? !r.match.matched
                                  : r.match.matched != options.invert;
        if (selected) {
            anySelected = true;
            ++counts[r.root];
        }
        progress.finished(selected, false);
        progress.out(formatFile(options, r.display, r.match));
    };

    // Runs on the walker thread.
    auto onError = [&](const std::string& p, const std::string& message) {
        anyError = true;
        progress.error();
        if (!options.noMessages) progress.err("mediagrep: " + p + ": " + message + "\n");
    };

    BoundedQueue<Job> pending(kMaxPendingPaths);
    std::thread walker([&] {
        for (std::size_t i = 0; i < options.paths.size(); ++i) {
            walk({options.paths[i]}, walkOptions,
                 [&](const std::string& p) {
                     progress.found();
                     pending.push({p, i});
                 },
                 onError);
        }
        progress.walkDone();
        pending.close();
    });
    {
        OrderedPool<Job, Result> pool(jobs, work, emit);
        while (std::optional<Job> job = pending.pop()) pool.submit(std::move(*job));
        pool.finish();
    }
    walker.join();
    progress.stop();

    if (options.output == Options::Output::Count && !options.quiet)
        for (std::size_t i = 0; i < options.paths.size(); ++i)
            write(options.paths[i] + ": " + std::to_string(counts[i]) + sep);
    std::fflush(stdout);

    if (anyError) return 2;
    return anySelected ? 0 : 1;
}

int realMain(const std::vector<std::string>& args) {
    std::string error;
    const std::optional<Options> options = parseArgs(args, error);
    if (!options) {
        warn(error);
        std::fputs(usageText().c_str(), stderr);
        return 2;
    }
    if (options->help) {
        write(usageText());
        return 0;
    }
    const std::optional<std::string> libraryVersion = MediaInfoSource::libraryVersion();
    if (options->version) {
        write("mediagrep " MEDIAGREP_VERSION "\n");
        write((libraryVersion ? *libraryVersion : std::string("MediaInfoLib: not found")) + "\n");
        return 0;
    }
    if (!libraryVersion) {
        warn("cannot load MediaInfoLib. Install MediaInfo (libmediainfo) and make sure "
             "its shared library is on the loader path.");
        return 2;
    }
    if (!MediaInfoSource::ensureUtf8Locale() && !options->noMessages)
        warn("warning: no UTF-8 locale is available; files with non-ASCII names may not open");
    if (options->fieldsFile) return dumpFields(*options);
    return run(*options);
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_BINARY);
    term::useUtf8Console();
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(narrow(argv[i]));
    return realMain(args);
}
#else
int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    return realMain(args);
}
#endif
