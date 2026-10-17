// MetadataSource backed by MediaInfoLib.
#pragma once

#include <memory>
#include <optional>
#include <string>

#include "eval.h"

namespace mediagrep {

// One instance per thread. Reusable across files: call open() for each.
class MediaInfoSource : public MetadataSource {
public:
    // parseSpeed is MediaInfoLib's ParseSpeed, 0 (headers only) to 1.
    explicit MediaInfoSource(double parseSpeed = 0.0);
    ~MediaInfoSource() override;
    MediaInfoSource(const MediaInfoSource&) = delete;
    MediaInfoSource& operator=(const MediaInfoSource&) = delete;

    // Reads the file's metadata. Returns false if the library could not
    // open or read it (missing file, permission denied, library missing).
    // A readable file that is not media opens fine; see isMedia().
    bool open(const std::string& utf8Path);

    // True when the last open() succeeded and the library recognized a
    // container or stream format.
    bool isMedia() const;

    std::size_t trackCount(TrackKind kind) const override;
    std::optional<FieldValue> lookup(TrackKind kind, std::size_t index,
                                     std::string_view name) const override;
    void forEachField(TrackKind kind, std::size_t index,
                      const std::function<void(const FieldValue&)>& fn) const override;

    // Library version string, or nullopt if the library cannot be loaded.
    static std::optional<std::string> libraryVersion();

    // On Unix, MediaInfoLib converts wide file names with wcstombs, so the
    // process needs a UTF-8 LC_CTYPE or files with non-ASCII names cannot be
    // opened. This sets one (environment first, then common UTF-8 locales),
    // once, leaving LC_NUMERIC alone. Returns false if none is available.
    // Constructing a MediaInfoSource calls it; main calls it to warn.
    static bool ensureUtf8Locale();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mediagrep
