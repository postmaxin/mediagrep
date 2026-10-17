// Adds mediagrep's File track, filled from stat(), on top of another source.
#pragma once

#include <string>
#include <vector>

#include "eval.h"

namespace mediagrep {

// Wraps `inner` (which must outlive this object) and answers for
// TrackKind::File itself. Fields, when the file can be examined:
//   Name, Directory, Extension, Size (byte), Modified, Created, Accessed
//   (all "YYYY-MM-DD HH:MM:SS UTC"), Mode (octal), Owner, Group.
// Fields the platform cannot supply are omitted.
class FileTrackSource : public MetadataSource {
public:
    FileTrackSource(const MetadataSource& inner, const std::string& path);

    std::size_t trackCount(TrackKind kind) const override;
    std::optional<FieldValue> lookup(TrackKind kind, std::size_t index,
                                     std::string_view name) const override;
    void forEachField(TrackKind kind, std::size_t index,
                      const std::function<void(const FieldValue&)>& fn) const override;

private:
    const MetadataSource& inner_;
    std::vector<FieldValue> fields_;
    bool present_ = false;
};

}  // namespace mediagrep
