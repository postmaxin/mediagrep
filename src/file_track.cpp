#include "file_track.h"

#include <cctype>
#include <cstdio>

#include "fsys.h"
#include "units.h"

namespace mediagrep {

namespace {

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

bool isSeparator(char c) {
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

// The path without its last component and the separator before it.
// "talk.mkv" -> "."; "/a/b.mkv" -> "/a"; "/b.mkv" -> "/".
std::string directoryOf(const std::string& path, const std::string& name) {
    std::string dir = path.substr(0, path.size() - name.size());
    while (dir.size() > 1 && isSeparator(dir.back())) dir.pop_back();
    if (dir.empty()) return ".";
    return dir;
}

}  // namespace

FileTrackSource::FileTrackSource(const MetadataSource& inner, const std::string& path) : inner_(inner) {
    const std::optional<fsys::FileInfo> info = fsys::stat(path);
    if (!info) return;
    present_ = true;

    auto text = [&](const char* field, std::string value) {
        if (!value.empty()) fields_.push_back({field, std::move(value), ValueType::Text, ""});
    };

    const std::string name = fsys::fileName(path);
    text("Name", name);
    text("Directory", directoryOf(path, name));
    const std::size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot != 0 && dot + 1 < name.size()) text("Extension", name.substr(dot + 1));

    fields_.push_back({"Size", std::to_string(info->size), ValueType::Integer, " byte"});
    if (info->modified) text("Modified", formatUtcTime(*info->modified));
    if (info->created) text("Created", formatUtcTime(*info->created));
    if (info->accessed) text("Accessed", formatUtcTime(*info->accessed));
    if (info->mode) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%04o", info->mode);
        text("Mode", buf);
    }
    text("Owner", info->owner);
    text("Group", info->group);
}

std::size_t FileTrackSource::trackCount(TrackKind kind) const {
    if (kind != TrackKind::File) return inner_.trackCount(kind);
    return present_ ? 1 : 0;
}

std::optional<FieldValue> FileTrackSource::lookup(TrackKind kind, std::size_t index,
                                                  std::string_view name) const {
    if (kind != TrackKind::File) return inner_.lookup(kind, index, name);
    if (!present_ || index != 0) return std::nullopt;
    for (const auto& f : fields_)
        if (iequals(f.name, name)) return f;
    return std::nullopt;
}

void FileTrackSource::forEachField(TrackKind kind, std::size_t index,
                                   const std::function<void(const FieldValue&)>& fn) const {
    if (kind != TrackKind::File) { inner_.forEachField(kind, index, fn); return; }
    if (!present_ || index != 0) return;
    for (const auto& f : fields_) fn(f);
}

}  // namespace mediagrep
