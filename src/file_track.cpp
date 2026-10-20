#include "file_track.h"

#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "fsys.h"
#include "path_track.h"
#include "units.h"
#ifdef _WIN32
#include "utf.h"
#endif

namespace mediagrep {

namespace {

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

bool isSeparator(char c, bool windowsStyle) { return c == '/' || (windowsStyle && c == '\\'); }

// True for "X:" at the end of `dir`, with nothing or a separator before the
// letter: a Windows drive, possibly behind "\\?\\".
bool endsWithDrive(const std::string& dir, bool windowsStyle) {
    const std::size_t n = dir.size();
    if (n < 2 || dir[n - 1] != ':' || !std::isalpha(static_cast<unsigned char>(dir[n - 2]))) return false;
    return n == 2 || isSeparator(dir[n - 3], windowsStyle);
}

#ifdef _WIN32
constexpr bool kWindowsStyle = true;
#else
constexpr bool kWindowsStyle = false;
#endif

}  // namespace

std::string directoryOfPath(const std::string& path, bool windowsStyle) {
    std::size_t cut = path.size();
    while (cut > 0 && !isSeparator(path[cut - 1], windowsStyle)) --cut;
    if (cut == 0) {
        // "E:clip.mkv" is relative to the current directory on drive E:.
        if (windowsStyle && path.size() > 2 && path[1] == ':' &&
            std::isalpha(static_cast<unsigned char>(path[0])))
            return path.substr(0, 2);
        return ".";
    }
    std::string dir = path.substr(0, cut);
    const char separator = dir.back();
    while (dir.size() > 1 && isSeparator(dir.back(), windowsStyle)) dir.pop_back();
    // "E:" alone means "the current directory on E:"; the root is "E:" plus its separator.
    if (windowsStyle && endsWithDrive(dir, windowsStyle)) dir += separator;
    return dir;
}

std::string recognitionDirectoryName(const std::string& directory) {
    const std::size_t sep = directory.find_last_of(kWindowsStyle ? "/\\" : "/");
    const std::string last = sep == std::string::npos ? directory : directory.substr(sep + 1);
    if (directory != "." && last != "." && last != "..") return last;
    // "." and ".." name nothing; ask the file system what they are.
#ifdef _WIN32
    const std::wstring wide = widen(directory);
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetFullPathNameW(wide.c_str(), static_cast<DWORD>(sizeof buf / sizeof *buf), buf, nullptr);
    if (n == 0 || n >= sizeof buf / sizeof *buf) return last;
    return fsys::fileName(narrow(std::wstring(buf, n)));
#else
    char buf[PATH_MAX];
    if (!::realpath(directory.c_str(), buf)) return last;
    return fsys::fileName(buf);
#endif
}

FileTrackSource::FileTrackSource(const MetadataSource& inner, const std::string& path) : inner_(inner) {
    const std::string name = fsys::fileName(path);
    const std::string directory = directoryOfPath(path, kWindowsStyle);
    path_ = recognizePath(name, recognitionDirectoryName(directory));

    const std::optional<fsys::FileInfo> info = fsys::stat(path);
    if (!info) return;
    filePresent_ = true;

    auto text = [&](const char* field, std::string value) {
        if (!value.empty()) file_.push_back({field, std::move(value), ValueType::Text, ""});
    };
    text("Name", name);
    text("Directory", directory);
    const std::size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot != 0 && dot + 1 < name.size()) text("Extension", name.substr(dot + 1));

    file_.push_back({"Size", std::to_string(info->size), ValueType::Integer, " byte"});
    if (info->modified) text("Modified", formatUtcTime(*info->modified));
    if (info->created) text("Created", formatUtcTime(*info->created));
    if (info->accessed) text("Accessed", formatUtcTime(*info->accessed));
    if (info->mode) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%04o", *info->mode);
        text("Mode", buf);
    }
    text("Owner", info->owner);
    text("Group", info->group);
}

const std::vector<FieldValue>* FileTrackSource::own(TrackKind kind) const {
    if (kind == TrackKind::File) return filePresent_ ? &file_ : nullptr;
    if (kind == TrackKind::Path) return path_.empty() ? nullptr : &path_;
    return nullptr;
}

std::size_t FileTrackSource::trackCount(TrackKind kind) const {
    if (kind != TrackKind::File && kind != TrackKind::Path) return inner_.trackCount(kind);
    return own(kind) ? 1 : 0;
}

std::optional<FieldValue> FileTrackSource::lookup(TrackKind kind, std::size_t index,
                                                  std::string_view name) const {
    if (kind != TrackKind::File && kind != TrackKind::Path) return inner_.lookup(kind, index, name);
    const auto* fields = own(kind);
    if (!fields || index != 0) return std::nullopt;
    for (const auto& f : *fields)
        if (iequals(f.name, name)) return f;
    return std::nullopt;
}

void FileTrackSource::forEachField(TrackKind kind, std::size_t index,
                                   const std::function<void(const FieldValue&)>& fn) const {
    if (kind != TrackKind::File && kind != TrackKind::Path) { inner_.forEachField(kind, index, fn); return; }
    const auto* fields = own(kind);
    if (!fields || index != 0) return;
    for (const auto& f : *fields) fn(f);
}

}  // namespace mediagrep
