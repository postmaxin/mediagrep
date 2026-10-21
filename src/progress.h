// Progress reporting on stderr, and the console lock all output goes
// through so a progress line never tears a match or a warning.
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace mediagrep {

struct ProgressState {
    std::size_t done = 0;     // files examined
    std::size_t found = 0;    // files discovered so far
    bool walking = true;      // still discovering, so `found` is a lower bound
    std::size_t matched = 0;  // files selected
    std::size_t errors = 0;
    double elapsed = 0;       // seconds
    std::string current;      // the file most recently started
};

// "0:42", "1:02:03".
std::string formatDuration(double seconds);

// Terminal columns a UTF-8 string occupies: one per code point, two for
// East Asian wide and fullwidth characters.
std::size_t displayWidth(std::string_view utf8);

// The single-line form, for a terminal `width` columns wide. Never reaches
// the last column, so the terminal does not wrap. No trailing newline.
std::string formatProgressLine(const ProgressState& state, std::size_t width);

// What to write to redraw the line on a terminal `columns` wide, given that
// `drawnWidth` columns were drawn before: the line, padded with spaces to
// cover the old one. The padding never goes past columns - 1, because a
// window shrunk since the last draw would wrap and a carriage return could not get
// back to the start of the line.
std::string redrawText(const std::string& line, std::size_t drawnWidth, std::size_t columns);

// What to write to erase a line drawn `drawnWidth` columns wide, capped the
// same way.
std::string clearText(std::size_t drawnWidth, std::size_t columns);

// The form logged periodically when stderr is not a terminal.
std::string formatProgressLog(const ProgressState& state);

class Progress {
public:
    enum class Mode {
        Off,   // no progress; output still serialized
        Line,  // one line on a terminal, redrawn in place
        Log,   // a plain line every logInterval, and a summary at the end
    };

    Progress(Mode mode, bool stdoutIsTerminal,
             std::chrono::milliseconds logInterval = std::chrono::seconds(10));
    ~Progress();
    Progress(const Progress&) = delete;
    Progress& operator=(const Progress&) = delete;

    void found();                          // a file was discovered
    void walkDone();                       // discovery finished
    void started(const std::string& path);
    void finished(bool selected, bool error);
    void error();                          // an error not tied to a file

    bool out(const std::string& text);     // write to stdout; false if it failed
    void err(const std::string& text);     // write to stderr

    // Clears the line (Line) or writes the summary (Log). Idempotent.
    void stop();

private:
    void loop();
    ProgressState snapshotLocked() const;
    void drawLocked();
    void clearLocked();

    const Mode mode_;
    const bool stdoutIsTerminal_;
    const std::chrono::milliseconds logInterval_;
    const std::chrono::steady_clock::time_point start_;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    ProgressState state_;
    bool stopping_ = false;
    bool stopped_ = false;
    bool drawn_ = false;
    std::size_t drawnWidth_ = 0;
    std::string drawnLine_;
    std::thread ticker_;
};

}  // namespace mediagrep
