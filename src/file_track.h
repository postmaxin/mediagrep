// Adds mediagrep's own tracks, File and Path, on top of another source.
#pragma once

#include <string>
#include <vector>

#include "eval.h"

namespace mediagrep {

// Exposed for tests. The path without its last component and the separator
// before it ("." for a bare name); a Windows drive root keeps its
// separator (drive E: stays "E:" plus its separator). `windowsStyle` makes the
// backslash a separator too; FileTrackSource passes the platform's.
std::string directoryOfPath(const std::string& path, bool windowsStyle);

// The name of `directory` as the Path recognizers should see it. A
// directory shown as "." or ending in ".." or "." says nothing, so it is
// resolved to the real parent's name (realpath, GetFullPathNameW). Anything
// else is just its last component.
std::string recognitionDirectoryName(const std::string& directory);

// Wraps `inner` (which must outlive this object) and answers for
// TrackKind::File and TrackKind::Path itself.
//
// File, from stat(), when the file can be examined:
//   Name, Directory, Extension, Size (byte), Modified, Created, Accessed
//   (all "YYYY-MM-DD HH:MM:SS UTC"), Mode (octal), Owner, Group.
//   Fields the platform cannot supply are omitted.
// Path, recognized from the file name (see path_track.h), present when
//   anything was recognized: When, Season, Episode, Id.
class FileTrackSource : public MetadataSource {
public:
    FileTrackSource(const MetadataSource& inner, const std::string& path);

    std::size_t trackCount(TrackKind kind) const override;
    std::optional<FieldValue> lookup(TrackKind kind, std::size_t index,
                                     std::string_view name) const override;
    void forEachField(TrackKind kind, std::size_t index,
                      const std::function<void(const FieldValue&)>& fn) const override;

private:
    const std::vector<FieldValue>* own(TrackKind kind) const;

    const MetadataSource& inner_;
    std::vector<FieldValue> file_;
    std::vector<FieldValue> path_;
    bool filePresent_ = false;
};

}  // namespace mediagrep
