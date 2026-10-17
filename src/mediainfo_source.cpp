#include "mediainfo_source.h"

#include <array>
#include <cctype>
#include <sstream>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#endif

// The vendored headers define a few function pointers they never use and
// pass NULL for size_t parameters.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wconversion-null"
#endif
#ifdef MEDIAGREP_LINK_STATIC
#include "MediaInfoDLL_Static.h"
#else
#include "MediaInfoDLL.h"
#endif
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include "utf.h"

namespace mediagrep {

namespace {

using MediaInfoDLL::MediaInfo;
using MediaInfoDLL::String;

MediaInfoDLL::stream_t toStream(TrackKind kind) {
    switch (kind) {
        case TrackKind::General: return MediaInfoDLL::Stream_General;
        case TrackKind::Video: return MediaInfoDLL::Stream_Video;
        case TrackKind::Audio: return MediaInfoDLL::Stream_Audio;
        case TrackKind::Text: return MediaInfoDLL::Stream_Text;
        case TrackKind::Image: return MediaInfoDLL::Stream_Image;
        case TrackKind::Menu: return MediaInfoDLL::Stream_Menu;
        case TrackKind::Other: return MediaInfoDLL::Stream_Other;
        case TrackKind::Any: break;
    }
    return MediaInfoDLL::Stream_General;
}

constexpr std::array<TrackKind, 7> kKinds{
    TrackKind::General, TrackKind::Video, TrackKind::Audio, TrackKind::Text,
    TrackKind::Image,   TrackKind::Menu,  TrackKind::Other,
};

std::size_t kindSlot(TrackKind kind) { return static_cast<std::size_t>(kind) - 1; }

// MediaInfoLib's Info_Options string: position InfoOption_TypeOfValue holds
// T (text), I (integer), F (float), D (date) or B (binary).
ValueType typeFromOptions(const std::string& options) {
    const std::size_t pos = MediaInfoDLL::InfoOption_TypeOfValue;
    if (options.size() <= pos) return ValueType::Text;
    switch (options[pos]) {
        case 'I': return ValueType::Integer;
        case 'F': return ValueType::Float;
        case 'D': return ValueType::Date;
        case 'B': return ValueType::Binary;
        default: return ValueType::Text;
    }
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

String pathToString(const std::filesystem::path& path) {
#ifdef _WIN32
    return path.wstring();
#else
    return widen(path.native());
#endif
}

}  // namespace

struct MediaInfoSource::Impl {
    MediaInfo mi;
    bool media = false;
    std::array<std::vector<std::vector<FieldValue>>, 7> tracks;

    explicit Impl(double parseSpeed) {
        std::ostringstream speed;
        speed << parseSpeed;
        mi.Option(__T("ParseSpeed"), widen(speed.str()));
        mi.Option(__T("ReadByHuman"), __T("0"));
    }

    void clear() {
        media = false;
        for (auto& t : tracks) t.clear();
    }

    // Copies every non-empty field of every track into the cache so the
    // file can be closed immediately.
    void snapshot() {
        for (TrackKind kind : kKinds) {
            const auto stream = toStream(kind);
            const std::size_t n = mi.Count_Get(stream);
            auto& list = tracks[kindSlot(kind)];
            list.resize(n);
            for (std::size_t idx = 0; idx < n; ++idx) {
                const std::size_t count = mi.Count_Get(stream, idx);
                auto& fields = list[idx];
                fields.reserve(count);
                for (std::size_t p = 0; p < count; ++p) {
                    std::string value = narrow(mi.Get(stream, idx, p, MediaInfoDLL::Info_Text));
                    if (value.empty()) continue;
                    FieldValue fv;
                    fv.name = narrow(mi.Get(stream, idx, p, MediaInfoDLL::Info_Name));
                    fv.value = std::move(value);
                    fv.type = typeFromOptions(narrow(mi.Get(stream, idx, p, MediaInfoDLL::Info_Options)));
                    fv.measure = narrow(mi.Get(stream, idx, p, MediaInfoDLL::Info_Measure));
                    fields.push_back(std::move(fv));
                }
            }
        }
    }

    const std::vector<FieldValue>* track(TrackKind kind, std::size_t index) const {
        if (kind == TrackKind::Any) return nullptr;
        const auto& list = tracks[kindSlot(kind)];
        return index < list.size() ? &list[index] : nullptr;
    }
};

MediaInfoSource::MediaInfoSource(double parseSpeed) : impl_(std::make_unique<Impl>(parseSpeed)) {}
MediaInfoSource::~MediaInfoSource() = default;

bool MediaInfoSource::open(const std::filesystem::path& path) {
    impl_->clear();
    if (impl_->mi.Open(pathToString(path)) == 0) return false;
    impl_->snapshot();
    impl_->mi.Close();
    const auto format = lookup(TrackKind::General, 0, "Format");
    impl_->media = format.has_value();
    return true;
}

bool MediaInfoSource::isMedia() const { return impl_->media; }

std::size_t MediaInfoSource::trackCount(TrackKind kind) const {
    if (kind == TrackKind::Any) return 0;
    return impl_->tracks[kindSlot(kind)].size();
}

std::optional<FieldValue> MediaInfoSource::lookup(TrackKind kind, std::size_t index,
                                                  std::string_view name) const {
    const auto* fields = impl_->track(kind, index);
    if (!fields) return std::nullopt;
    for (const auto& f : *fields)
        if (iequals(f.name, name)) return f;
    return std::nullopt;
}

void MediaInfoSource::forEachField(TrackKind kind, std::size_t index,
                                   const std::function<void(const FieldValue&)>& fn) const {
    const auto* fields = impl_->track(kind, index);
    if (!fields) return;
    for (const auto& f : *fields) fn(f);
}

std::optional<std::string> MediaInfoSource::libraryVersion() {
    const std::string v = narrow(MediaInfo::Option_Static(__T("Info_Version")));
    if (v.find("MediaInfoLib") == std::string::npos) return std::nullopt;
    return v;
}

}  // namespace mediagrep
