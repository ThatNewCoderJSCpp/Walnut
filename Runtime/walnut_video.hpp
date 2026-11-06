#ifndef WALNUT_VIDEO_HPP
#define WALNUT_VIDEO_HPP

#include "walnut_runtime.hpp"
#include "walnut_audio.hpp"
#include "fizmo/../../Source/Video/decoder.hpp"
#include "fizmo/../../Source/Video/demux.hpp"
#include "fizmo/../../Source/Video/frame.hpp"
#include "fizmo/../../Source/Video/h264.hpp"
#include "fizmo/../../Source/Video/player.hpp"
#include "fizmo/../../Source/Audio/container.hpp"
#include "fizmo/../../Source/Images/Bitmap/image.hpp"
#include "fizmo/../../Source/Images/Formats/image_formats.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace walnut_rt {
namespace video {

namespace fv = fizmo::video;
namespace fa = fizmo::audio;

using F = walnut_rt::f128;
using walnut_rt::audio::d;
using walnut_rt::audio::w;

inline std::string& last_error() {
    thread_local std::string e;
    return e;
}

inline void set_error(const std::string& e) { last_error() = e; }

inline std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

inline long long media_type_number(fv::MediaType t) {
    switch (t) {
        case fv::MediaType::video: return 0;
        case fv::MediaType::audio: return 1;
        case fv::MediaType::subtitle: return 2;
        default: return 3;
    }
}

inline fv::MediaType media_type_of(long long t) {
    switch (t) {
        case 0: return fv::MediaType::video;
        case 1: return fv::MediaType::audio;
        case 2: return fv::MediaType::subtitle;
        default: return fv::MediaType::other;
    }
}

inline long long pixel_format_number(fv::PixelFormat p) {
    switch (p) {
        case fv::PixelFormat::gray: return 1;
        case fv::PixelFormat::yuv420p: return 2;
        case fv::PixelFormat::yuv422p: return 3;
        case fv::PixelFormat::yuv444p: return 4;
        case fv::PixelFormat::gray10: return 5;
        case fv::PixelFormat::yuv420p10: return 6;
        case fv::PixelFormat::yuv422p10: return 7;
        case fv::PixelFormat::yuv444p10: return 8;
        case fv::PixelFormat::gray12: return 9;
        case fv::PixelFormat::yuv420p12: return 10;
        case fv::PixelFormat::yuv422p12: return 11;
        case fv::PixelFormat::yuv444p12: return 12;
        case fv::PixelFormat::gray14: return 13;
        case fv::PixelFormat::yuv420p14: return 14;
        case fv::PixelFormat::yuv422p14: return 15;
        case fv::PixelFormat::yuv444p14: return 16;
        case fv::PixelFormat::yuv440p: return 17;
        case fv::PixelFormat::yuv440p10: return 18;
        case fv::PixelFormat::yuv440p12: return 19;
        default: return 0;
    }
}

inline long long state_number(fv::PlayerState s) {
    switch (s) {
        case fv::PlayerState::paused: return 1;
        case fv::PlayerState::playing: return 2;
        case fv::PlayerState::ended: return 3;
        case fv::PlayerState::error: return 4;
        default: return 0;
    }
}

inline long long event_number(fv::PlayerEventType t) { return static_cast<long long>(t); }

inline long long deinterlace_number(fv::DeinterlaceSetting s) {
    switch (s) {
        case fv::DeinterlaceSetting::off: return 0;
        case fv::DeinterlaceSetting::on: return 2;
        default: return 1;
    }
}

inline fv::DeinterlaceSetting deinterlace_of(long long s) {
    switch (s) {
        case 0: return fv::DeinterlaceSetting::off;
        case 2: return fv::DeinterlaceSetting::on;
        default: return fv::DeinterlaceSetting::automatic;
    }
}

inline long long deinterlace_mode_number(fv::DeinterlaceMode m) {
    switch (m) {
        case fv::DeinterlaceMode::weave: return 0;
        case fv::DeinterlaceMode::bob: return 1;
        case fv::DeinterlaceMode::linear: return 2;
        case fv::DeinterlaceMode::yadif_spatial: return 4;
        default: return 3;
    }
}

inline fv::DeinterlaceMode deinterlace_mode_of(long long m) {
    switch (m) {
        case 0: return fv::DeinterlaceMode::weave;
        case 1: return fv::DeinterlaceMode::bob;
        case 2: return fv::DeinterlaceMode::linear;
        case 4: return fv::DeinterlaceMode::yadif_spatial;
        default: return fv::DeinterlaceMode::yadif;
    }
}

inline std::string join_tags(const std::vector<std::pair<std::string, std::string>>& tags, const std::string& key) {
    const std::string k = lower(key);
    for (const auto& t : tags) {
        if (lower(t.first) == k) return t.second;
    }
    return std::string();
}

inline std::vector<std::string> tag_keys_of(const std::vector<std::pair<std::string, std::string>>& tags) {
    std::vector<std::string> out;
    for (const auto& t : tags) out.push_back(t.first);
    return out;
}

inline bool track_supported(const fv::TrackInfo& t) {
    if (t.type == fv::MediaType::video) return fv::video_codec_supported(t.codec);
    if (t.type == fv::MediaType::audio) return fa::container_audio_codec_supported(t.codec);
    return false;
}

inline std::string codec_display(const fv::TrackInfo& t) {
    if (t.type == fv::MediaType::video) return fv::video_codec_display_name(t.codec);
    return t.codec;
}

struct api {};

class track {
public:
    track() = default;
    track(fv::TrackInfo t, long long index) : m(std::move(t)), m_index(index) {}

    bool valid() const { return m_index >= 0; }
    long long index() const { return m_index; }
    long long id() const { return static_cast<long long>(m.id); }
    long long kind() const { return media_type_number(m.type); }
    std::string kind_name() const { return fv::media_type_name(m.type); }
    std::string codec() const { return m.codec; }
    std::string codec_name() const { return codec_display(m); }
    bool supported() const { return track_supported(m); }
    std::string language() const { return m.language; }
    std::string name() const { return m.name; }
    bool is_default() const { return m.is_default; }
    long long width() const { return static_cast<long long>(m.width); }
    long long height() const { return static_cast<long long>(m.height); }
    long long display_width() const { return static_cast<long long>(m.display_width ? m.display_width : m.width); }
    long long display_height() const { return static_cast<long long>(m.display_height ? m.display_height : m.height); }
    long long rotation() const { return static_cast<long long>(m.rotation); }
    F frame_rate() const { return w(m.frame_rate); }
    long long sample_rate() const { return static_cast<long long>(m.sample_rate); }
    long long channels() const { return static_cast<long long>(m.channels); }
    long long bits_per_sample() const { return static_cast<long long>(m.bits_per_sample); }
    long long bitrate() const { return static_cast<long long>(m.bitrate); }
    F duration() const { return w(m.duration); }
    long long frame_count() const { return static_cast<long long>(m.frame_count); }
    std::vector<std::uint8_t> codec_config() const { return m.codec_config; }

    fv::TrackInfo m;

private:
    long long m_index = -1;
};

class media_info {
public:
    media_info() = default;

    static media_info probe(const std::string& path) {
        media_info out;
        out.m = fv::probe_media(path);
        return out;
    }

    static media_info probe_bytes(const std::vector<std::uint8_t>& data) {
        media_info out;
        if (data.empty()) {
            out.m.error = "no data";
            return out;
        }
        out.m = fv::probe_media(data.data(), data.size());
        return out;
    }

    bool valid() const { return m.valid; }
    std::string error() const { return m.error; }
    std::string container() const { return m.container; }
    F duration() const { return w(m.duration); }
    long long track_count() const { return static_cast<long long>(m.tracks.size()); }

    track at(long long i) const {
        if (i < 0 || static_cast<std::size_t>(i) >= m.tracks.size()) return track();
        return track(m.tracks[static_cast<std::size_t>(i)], i);
    }

    std::string tag(const std::string& key) const { return join_tags(m.tags, key); }
    std::vector<std::string> tag_keys() const { return tag_keys_of(m.tags); }
    long long video_track() const { return static_cast<long long>(m.video_track); }
    long long audio_track() const { return static_cast<long long>(m.audio_track); }
    bool video_supported() const { return m.video_supported; }
    bool audio_supported() const { return m.audio_supported; }
    std::string video_codec() const { return m.video_codec; }
    std::string audio_codec() const { return m.audio_codec; }
    std::string video_error() const { return m.video_error; }
    std::string audio_error() const { return m.audio_error; }
    long long width() const { return static_cast<long long>(m.width); }
    long long height() const { return static_cast<long long>(m.height); }
    long long display_width() const { return static_cast<long long>(m.display_width); }
    long long display_height() const { return static_cast<long long>(m.display_height); }
    F display_aspect() const { return w(m.display_aspect); }
    long long rotation() const { return static_cast<long long>(m.rotation); }
    F frame_rate() const { return w(m.frame_rate); }
    long long sample_rate() const { return static_cast<long long>(m.sample_rate); }
    long long channels() const { return static_cast<long long>(m.channels); }

    fv::MediaInfo m;
};

inline long long packed_at(const std::vector<std::uint8_t>& rgba, std::uint32_t width, long long x, long long y) {
    const std::size_t at = (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 4u;
    return static_cast<long long>((static_cast<std::uint32_t>(rgba[at]) << 24) | (static_cast<std::uint32_t>(rgba[at + 1]) << 16) | (static_cast<std::uint32_t>(rgba[at + 2]) << 8) | static_cast<std::uint32_t>(rgba[at + 3]));
}

inline std::string save_rgba(const std::string& path, std::uint32_t width, std::uint32_t height, const std::vector<std::uint8_t>& rgba, long long quality) {
    if (!width || !height || rgba.size() != static_cast<std::size_t>(width) * height * 4u) return "the picture is empty";
    const fizmo::images::BitmapImage img = fizmo::images::BitmapImage::from_rgba(width, height, rgba.data());
    fizmo::images::EncodeOptions options;
    options.quality = static_cast<int>(std::max(1LL, std::min(100LL, quality)));
    std::string why;
    if (!fizmo::images::save_image(path, img, fizmo::images::ImageFormat::Unknown, options, &why)) return why.empty() ? std::string("can't write the image") : why;
    return std::string();
}

class picture {
public:
    picture() = default;
    picture(std::vector<std::uint8_t> rgba, std::uint32_t width, std::uint32_t height, std::uint64_t serial, double time)
        : m_rgba(std::make_shared<const std::vector<std::uint8_t>>(std::move(rgba))), m_width(width), m_height(height), m_serial(serial), m_time(time) {}

    bool empty() const { return !m_rgba || m_rgba->empty(); }
    long long width() const { return static_cast<long long>(m_width); }
    long long height() const { return static_cast<long long>(m_height); }
    long long serial() const { return static_cast<long long>(m_serial); }
    F time() const { return w(m_time); }
    long long get(long long x, long long y) const { return packed_at(*m_rgba, m_width, x, y); }
    std::vector<std::uint8_t> pixels() const { return m_rgba ? *m_rgba : std::vector<std::uint8_t>(); }
    std::string save(const std::string& path, long long quality) const { return m_rgba ? save_rgba(path, m_width, m_height, *m_rgba, quality) : std::string("the picture is empty"); }

private:
    std::shared_ptr<const std::vector<std::uint8_t>> m_rgba;
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
    std::uint64_t m_serial = 0;
    double m_time = 0.0;
};

class frame {
public:
    frame() = default;
    frame(fv::VideoFrame f, std::uint64_t serial) : m(std::make_shared<shared>()), m_serial(serial) { m->f = std::move(f); }

    bool empty() const { return !m || m->f.empty(); }
    long long width() const { return m ? static_cast<long long>(m->f.width) : 0; }
    long long height() const { return m ? static_cast<long long>(m->f.height) : 0; }
    F pts() const { return w(m ? m->f.pts : 0.0); }
    bool keyframe() const { return m && m->f.keyframe; }
    long long serial() const { return static_cast<long long>(m_serial); }
    long long format() const { return m ? pixel_format_number(m->f.format) : 0; }
    std::string format_name() const { return fv::format_name(m ? m->f.format : fv::PixelFormat::none); }
    long long bit_depth() const { return m ? static_cast<long long>(fv::bit_depth(m->f.format)) : 0; }
    long long plane_count() const { return m ? static_cast<long long>(fv::plane_count(m->f.format)) : 0; }
    long long bytes_per_sample() const { return m ? static_cast<long long>(fv::bytes_per_sample(m->f.format)) : 0; }
    long long plane_width(long long i) const { return has_plane(i) ? static_cast<long long>(m->f.plane_width(static_cast<int>(i))) : 0; }
    long long plane_height(long long i) const { return has_plane(i) ? static_cast<long long>(m->f.plane_height(static_cast<int>(i))) : 0; }
    long long sar_num() const { return m ? static_cast<long long>(m->f.sar_num) : 1; }
    long long sar_den() const { return m ? static_cast<long long>(m->f.sar_den) : 1; }
    F display_aspect() const { return w(m ? m->f.display_aspect() : 0.0); }
    bool full_range() const { return m && m->f.color.range == fv::ColorRange::full; }
    long long chroma_shift_x() const { return m ? static_cast<long long>(fv::chroma_shift_x(m->f.format)) : 0; }
    long long chroma_shift_y() const { return m ? static_cast<long long>(fv::chroma_shift_y(m->f.format)) : 0; }
    long long max_sample_value() const { return m ? static_cast<long long>(fv::max_sample_value(m->f.format)) : 0; }
    bool interlaced() const { return m && m->f.interlaced; }
    bool top_field_first() const { return !m || m->f.top_field_first; }
    std::string structure_name() const { return fv::picture_structure_name(m ? m->f.structure : fv::PictureStructure::frame); }
    long long repeat_fields() const { return m ? static_cast<long long>(m->f.repeat_fields) : 0; }

    std::string color_matrix() const {
        if (!m) return "unspecified";
        switch (m->f.color.matrix) {
            case fv::ColorMatrix::bt601: return "bt601";
            case fv::ColorMatrix::bt709: return "bt709";
            case fv::ColorMatrix::bt2020: return "bt2020";
            case fv::ColorMatrix::rgb: return "rgb";
            default: return "unspecified";
        }
    }

    std::vector<std::uint8_t> plane(long long i) const {
        std::vector<std::uint8_t> out;
        if (!has_plane(i)) return out;
        const int p = static_cast<int>(i);
        const std::size_t row = static_cast<std::size_t>(m->f.plane_width(p)) * static_cast<std::size_t>(fv::bytes_per_sample(m->f.format));
        const std::size_t rows = m->f.plane_height(p);
        out.resize(row * rows);
        const std::uint8_t* src = m->f.plane(p);
        for (std::size_t y = 0; y < rows; ++y) std::copy(src + y * m->f.stride[p], src + y * m->f.stride[p] + row, out.data() + y * row);
        return out;
    }

    long long sample(long long i, long long x, long long y) const {
        const int p = static_cast<int>(i);
        const std::uint8_t* row = m->f.plane(p) + static_cast<std::size_t>(y) * m->f.stride[p];
        if (fv::bytes_per_sample(m->f.format) == 2) {
            const std::uint8_t* s = row + static_cast<std::size_t>(x) * 2u;
            return static_cast<long long>(s[0] | (static_cast<unsigned>(s[1]) << 8));
        }
        return static_cast<long long>(row[static_cast<std::size_t>(x)]);
    }

    long long get(long long x, long long y) const { return packed_at(rgba_ref(), m->f.width, x, y); }
    std::vector<std::uint8_t> rgba() const { return empty() ? std::vector<std::uint8_t>() : rgba_ref(); }
    picture to_picture() const { return empty() ? picture() : picture(rgba_ref(), m->f.width, m->f.height, m_serial, m->f.pts); }
    std::string save(const std::string& path, long long quality) const { return empty() ? std::string("the frame is empty") : save_rgba(path, m->f.width, m->f.height, rgba_ref(), quality); }

    const fv::VideoFrame* raw() const { return m ? &m->f : nullptr; }

private:
    struct shared {
        fv::VideoFrame f;
        std::once_flag once;
        std::vector<std::uint8_t> rgba;
    };

    bool has_plane(long long i) const { return m && !m->f.empty() && i >= 0 && i < fv::plane_count(m->f.format); }

    const std::vector<std::uint8_t>& rgba_ref() const {
        shared* s = m.get();
        std::call_once(s->once, [s]() { s->rgba = fv::to_rgba(s->f); });
        return s->rgba;
    }

    std::shared_ptr<shared> m;
    std::uint64_t m_serial = 0;
};

inline frame extract(const std::string& path, const F& seconds) {
    fv::VideoFrame f;
    std::string error;
    set_error(std::string());
    if (!fv::extract_frame(path, std::max(0.0, d(seconds)), f, &error)) {
        set_error(error.empty() ? std::string("unknown error") : error);
        return frame();
    }
    return frame(std::move(f), 0);
}

inline frame extract_bytes(const std::vector<std::uint8_t>& data, const F& seconds) {
    fv::VideoFrame f;
    std::string error;
    set_error(std::string());
    if (data.empty() || !fv::extract_frame(data.data(), data.size(), std::max(0.0, d(seconds)), f, &error)) {
        set_error(error.empty() ? std::string("no data") : error);
        return frame();
    }
    return frame(std::move(f), 0);
}

inline std::vector<std::uint8_t> read_file(const std::string& path) {
    std::vector<std::uint8_t> out;
    set_error(std::string());
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        set_error("the file can't be opened");
        return out;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size > 0) {
        out.resize(static_cast<std::size_t>(size));
        in.read(reinterpret_cast<char*>(out.data()), size);
        if (!in) {
            out.clear();
            set_error("the file can't be read");
        }
    }
    return out;
}

inline bool codec_supported(const std::string& codec) { return fv::video_codec_supported(codec) || fa::container_audio_codec_supported(codec); }
inline bool video_codec_supported(const std::string& codec) { return fv::video_codec_supported(codec); }
inline bool audio_codec_supported(const std::string& codec) { return fa::container_audio_codec_supported(codec); }
inline std::string codec_name(const std::string& codec) { return fv::video_codec_display_name(codec); }
inline std::vector<std::string> supported_video_codecs() { return fv::supported_video_codecs(); }

inline std::string container_of(const std::vector<std::uint8_t>& data) { return data.empty() ? std::string() : fv::detect_container(data.data(), data.size()); }

class packet {
public:
    packet() = default;
    explicit packet(fv::Packet p) : m(std::make_shared<const fv::Packet>(std::move(p))) {}

    bool valid() const { return static_cast<bool>(m); }
    long long track() const { return m ? static_cast<long long>(m->track) : -1; }
    F pts() const { return w(m ? m->pts : 0.0); }
    F dts() const { return w(m ? m->dts : 0.0); }
    F duration() const { return w(m ? m->duration : 0.0); }
    bool keyframe() const { return m && m->keyframe; }
    long long position() const { return m ? static_cast<long long>(m->position) : 0; }
    long long size() const { return m ? static_cast<long long>(m->data.size()) : 0; }
    std::vector<std::uint8_t> data() const { return m ? m->data : std::vector<std::uint8_t>(); }
    F discard_padding() const { return w(m ? m->discard_padding : 0.0); }

    const fv::Packet* raw() const { return m.get(); }

private:
    std::shared_ptr<const fv::Packet> m;
};

class demuxer {
public:
    demuxer() : m(std::make_shared<fv::Demuxer>()) {}

    std::string open(const std::string& path) {
        std::string error;
        if (!m->open(path, &error)) return error.empty() ? std::string("unknown error") : error;
        return std::string();
    }

    std::string open_bytes(const std::vector<std::uint8_t>& data) {
        if (data.empty()) return "no data";
        std::string error;
        if (!m->open_shared(std::make_shared<const std::vector<std::uint8_t>>(data), &error)) return error.empty() ? std::string("unknown error") : error;
        return std::string();
    }

    void close() { m->close(); }
    bool is_open() const { return m->is_open(); }
    long long track_count() const { return static_cast<long long>(m->tracks().size()); }

    track at(long long i) const {
        if (i < 0 || static_cast<std::size_t>(i) >= m->tracks().size()) return track();
        return track(m->tracks()[static_cast<std::size_t>(i)], i);
    }

    F duration() const { return w(m->duration()); }
    std::string format_name() const { return m->format_name(); }
    std::string tag(const std::string& key) const { return join_tags(m->tags(), key); }
    std::vector<std::string> tag_keys() const { return tag_keys_of(m->tags()); }
    long long best_track(long long kind) const { return static_cast<long long>(m->best_track(media_type_of(kind))); }
    void select(long long i, bool on) { if (i >= 0) m->select(static_cast<std::uint32_t>(i), on); }
    bool selected(long long i) const { return i >= 0 && m->selected(static_cast<std::uint32_t>(i)); }

    packet read() {
        fv::Packet p;
        if (!m->read(p)) return packet();
        return packet(std::move(p));
    }

    bool seek(const F& seconds, bool to_keyframe_before) { return m->seek(d(seconds), to_keyframe_before); }
    bool rewind() { return m->rewind(); }

private:
    std::shared_ptr<fv::Demuxer> m;
};

class decoder {
public:
    decoder() = default;

    static decoder create(const track& t) {
        decoder out;
        std::string error;
        set_error(std::string());
        std::unique_ptr<fv::VideoDecoder> made = fv::create_video_decoder(t.m, &error);
        if (!made) {
            set_error(error.empty() ? std::string("unsupported codec ") + t.m.codec : error);
            return out;
        }
        out.m = std::shared_ptr<fv::VideoDecoder>(std::move(made));
        return out;
    }

    bool valid() const { return static_cast<bool>(m); }

    std::string decode(const packet& p) {
        if (!m) return "the decoder isn't open";
        if (!p.raw()) return "the packet is empty";
        std::string error;
        if (!m->decode_packet(*p.raw(), &error)) return error.empty() ? std::string("the packet couldn't be decoded") : error;
        return std::string();
    }

    std::string decode_bytes(const std::vector<std::uint8_t>& data, const F& pts) {
        if (!m) return "the decoder isn't open";
        std::string error;
        if (!m->decode(data.data(), data.size(), d(pts), &error)) return error.empty() ? std::string("the data couldn't be decoded") : error;
        return std::string();
    }

    frame receive() {
        fv::VideoFrame f;
        if (!m || !m->receive(f)) return frame();
        return frame(std::move(f), 0);
    }

    void flush() { if (m) m->flush(); }
    void reset() { if (m) m->reset(); }
    std::string codec() const { return m ? std::string(m->codec()) : std::string(); }
    long long width() const { return m ? static_cast<long long>(m->width()) : 0; }
    long long height() const { return m ? static_cast<long long>(m->height()) : 0; }
    long long frames_decoded() const { return m ? static_cast<long long>(m->frames_decoded()) : 0; }
    long long error_count() const { return m ? static_cast<long long>(m->error_count()) : 0; }

private:
    std::shared_ptr<fv::VideoDecoder> m;
};

class h264_decoder {
public:
    h264_decoder() : m(std::make_shared<fv::H264Decoder>()) {}

    std::string open(const std::vector<std::uint8_t>& avcc) {
        std::string error;
        if (!m->open(avcc.data(), avcc.size(), &error)) return error.empty() ? std::string("the avcC configuration couldn't be read") : error;
        return std::string();
    }

    bool open_annexb() { return m->open_annexb(); }

    std::string decode(const std::vector<std::uint8_t>& data, const F& pts) {
        std::string error;
        if (!m->decode(data.data(), data.size(), d(pts), &error)) return error.empty() ? std::string("the data couldn't be decoded") : error;
        return std::string();
    }

    frame receive() {
        fv::VideoFrame f;
        if (!m->receive(f)) return frame();
        return frame(std::move(f), 0);
    }

    void flush() { m->flush(); }
    void reset() { m->reset(); }
    void close() { m->close(); }
    bool is_open() const { return m->is_open(); }
    long long profile() const { return m->info().profile_idc; }
    long long level() const { return m->info().level_idc; }
    long long chroma_format() const { return m->info().chroma_format_idc; }
    long long bit_depth() const { return m->info().bit_depth; }
    long long width() const { return static_cast<long long>(m->info().width); }
    long long height() const { return static_cast<long long>(m->info().height); }
    long long coded_width() const { return static_cast<long long>(m->info().coded_width); }
    long long coded_height() const { return static_cast<long long>(m->info().coded_height); }
    bool progressive() const { return m->info().progressive; }
    bool cabac() const { return m->info().cabac; }
    F frame_rate() const { return w(m->info().frame_rate); }
    long long frames_decoded() const { return static_cast<long long>(m->frames_decoded()); }
    long long error_count() const { return static_cast<long long>(m->error_count()); }

private:
    std::shared_ptr<fv::H264Decoder> m;
};

class h264_reader {
public:
    h264_reader() : m(std::make_shared<fv::H264FileReader>()) {}

    std::string open(const std::string& path) {
        std::string error;
        if (!m->open(path, &error)) return error.empty() ? std::string("unknown error") : error;
        return std::string();
    }

    std::string open_bytes(const std::vector<std::uint8_t>& data) {
        if (data.empty() || !m->open(data.data(), data.size())) return "no H.264 start code in the data";
        return std::string();
    }

    std::vector<std::uint8_t> read() {
        std::vector<std::uint8_t> out;
        if (!m->read(out)) out.clear();
        return out;
    }

    void rewind() { m->rewind(); }
    void close() { m->close(); }
    bool is_open() const { return m->is_open(); }

private:
    std::shared_ptr<fv::H264FileReader> m;
};

inline bool is_annexb(const std::vector<std::uint8_t>& data) { return !data.empty() && fv::is_annexb_h264(data.data(), data.size()); }

class event {
public:
    event() = default;
    explicit event(fv::PlayerEvent e) : m(std::move(e)) {}

    long long type() const { return event_number(m.type); }
    std::string name() const { return fv::player_event_name(m.type); }
    F position() const { return w(m.position); }
    std::string message() const { return m.message; }

private:
    fv::PlayerEvent m;
};

class event_list {
public:
    event_list() = default;
    explicit event_list(std::vector<fv::PlayerEvent> e) : m(std::move(e)) {}

    long long size() const { return static_cast<long long>(m.size()); }
    event at(long long i) const { return event(m[static_cast<std::size_t>(i)]); }

private:
    std::vector<fv::PlayerEvent> m;
};

class stats {
public:
    stats() = default;
    explicit stats(fv::PlayerStats s) : m(s) {}

    long long frames_decoded() const { return static_cast<long long>(m.frames_decoded); }
    long long frames_presented() const { return static_cast<long long>(m.frames_presented); }
    long long frames_dropped() const { return static_cast<long long>(m.frames_dropped); }
    long long frames_skipped() const { return static_cast<long long>(m.frames_skipped); }
    long long packets_skipped() const { return static_cast<long long>(m.packets_skipped); }
    long long decode_errors() const { return static_cast<long long>(m.decode_errors); }
    long long audio_underruns() const { return static_cast<long long>(m.audio_underruns); }
    long long queued_packets() const { return static_cast<long long>(m.queued_packets); }
    long long queued_frames() const { return static_cast<long long>(m.queued_frames); }
    F audio_buffered() const { return w(m.audio_buffered); }
    F average_decode_ms() const { return w(m.average_decode_ms); }
    F average_convert_ms() const { return w(m.average_convert_ms); }
    bool audio_clock() const { return m.audio_clock; }
    long long frames_deinterlaced() const { return static_cast<long long>(m.frames_deinterlaced); }
    bool deinterlacing() const { return m.deinterlacing; }

private:
    fv::PlayerStats m;
};

template <typename Fn>
inline void call_with_event(Fn& fn, event& e) {
    using A = walnut_rt::audio::arg_of<Fn, 0>;
    if constexpr (std::is_same_v<A, event>) {
        fn(e);
    } else {
        A wrapped(walnut_rt::native_tag{}, e);
        fn(wrapped);
    }
}

class player {
public:
    player() : m(std::make_shared<state>()) {}

    void configure(bool enable_video, bool enable_audio, bool rgba, bool autoplay, bool loop, bool preserve_pitch, const F& volume, bool muted, const F& rate, long long max_queued_frames, const F& presentation_offset, const F& audio_buffer_seconds, long long sample_rate, long long channels, const std::string& device, long long latency_ms) {
        fv::PlayerOptions o;
        o.enable_video = enable_video;
        o.enable_audio = enable_audio;
        o.rgba = rgba;
        o.autoplay = autoplay;
        o.loop = loop;
        o.preserve_pitch = preserve_pitch;
        o.volume = static_cast<float>(std::max(0.0, d(volume)));
        o.muted = muted;
        o.rate = d(rate);
        o.max_queued_frames = max_queued_frames <= 0 ? 0u : static_cast<std::size_t>(max_queued_frames);
        o.presentation_offset = d(presentation_offset);
        o.audio_buffer_seconds = std::max(0.05, d(audio_buffer_seconds));
        o.output_sample_rate = walnut_rt::audio::rate_of(sample_rate);
        o.output_channels = static_cast<std::uint32_t>(std::max<long long>(0, std::min<long long>(channels, 8)));
        o.output = walnut_rt::audio::make_config(fa::DeviceKind::output, device, sample_rate, channels, latency_ms, "walnut video");
        m->native->set_options(o);
    }

    void configure_deinterlace(long long setting, long long mode, bool field_rate) {
        fv::PlayerOptions o = m->native->options();
        o.deinterlace = deinterlace_of(setting);
        o.deinterlace_mode = deinterlace_mode_of(mode);
        o.deinterlace_field_rate = field_rate;
        m->native->set_options(o);
    }

    void set_deinterlace_value(long long setting) { m->native->set_deinterlace(deinterlace_of(setting)); }
    long long deinterlace_value() const { return deinterlace_number(m->native->deinterlace()); }
    std::string deinterlace_name() const { return fv::deinterlace_setting_name(m->native->deinterlace()); }
    void set_deinterlace_mode_value(long long mode, bool field_rate) { m->native->set_deinterlace_mode(deinterlace_mode_of(mode), field_rate); }
    long long deinterlace_mode_value() const { return deinterlace_mode_number(m->native->deinterlace_mode()); }
    std::string deinterlace_mode_name() const { return fv::deinterlace_mode_name(m->native->deinterlace_mode()); }
    bool deinterlace_field_rate() const { return m->native->deinterlace_field_rate(); }
    bool is_deinterlacing() const { return m->native->is_deinterlacing(); }

    std::string open(const std::string& path) {
        std::string error;
        if (!m->native->open(path, &error)) return error.empty() ? std::string("unknown error") : error;
        reset_serials();
        return std::string();
    }

    std::string open_bytes(const std::vector<std::uint8_t>& data) {
        if (data.empty()) return "no data";
        std::string error;
        if (!m->native->open_shared(std::make_shared<const std::vector<std::uint8_t>>(data), &error)) return error.empty() ? std::string("unknown error") : error;
        reset_serials();
        return std::string();
    }

    void close() {
        m->native->close();
        reset_serials();
    }

    bool is_open() const { return m->native->is_open(); }
    media_info info() const {
        media_info out;
        out.m = m->native->info();
        return out;
    }

    long long track_count() const { return static_cast<long long>(m->native->tracks().size()); }

    track at(long long i) const {
        const std::vector<fv::TrackInfo>& t = m->native->tracks();
        if (i < 0 || static_cast<std::size_t>(i) >= t.size()) return track();
        return track(t[static_cast<std::size_t>(i)], i);
    }

    F duration() const { return w(m->native->duration()); }
    bool has_video() const { return m->native->has_video(); }
    bool has_audio() const { return m->native->has_audio(); }
    long long width() const { return static_cast<long long>(m->native->width()); }
    long long height() const { return static_cast<long long>(m->native->height()); }
    long long display_width() const { return static_cast<long long>(m->native->display_width()); }
    long long display_height() const { return static_cast<long long>(m->native->display_height()); }
    F display_aspect() const { return w(m->native->display_aspect()); }
    long long rotation() const { return static_cast<long long>(m->native->rotation()); }
    F frame_rate() const { return w(m->native->frame_rate()); }
    long long video_track() const { return static_cast<long long>(m->native->video_track()); }
    long long audio_track() const { return static_cast<long long>(m->native->audio_track()); }

    std::string select_video_track(long long index) {
        std::string error;
        if (!m->native->select_video_track(static_cast<int>(index), &error)) return error.empty() ? std::string("can't use that track") : error;
        return std::string();
    }

    std::string select_audio_track(long long index) {
        std::string error;
        if (!m->native->select_audio_track(static_cast<int>(index), &error)) return error.empty() ? std::string("can't use that track") : error;
        return std::string();
    }

    std::string video_error() const { return m->native->video_error(); }
    std::string audio_error() const { return m->native->audio_error(); }

    void play() { m->native->play(); }
    void pause() { m->native->pause(); }
    void stop() { m->native->stop(); }
    bool seek(const F& seconds, bool accurate) { return m->native->seek(std::max(0.0, d(seconds)), accurate); }
    F position() const { return w(m->native->position()); }
    long long state_value() const { return state_number(m->native->state()); }
    std::string state_name() const { return fv::player_state_name(m->native->state()); }
    bool is_playing() const { return m->native->is_playing(); }
    bool is_paused() const { return m->native->is_paused(); }
    bool is_seeking() const { return m->native->is_seeking(); }
    bool is_buffering() const { return m->native->is_buffering(); }
    bool ended() const { return m->native->ended(); }
    std::string error() const { return m->native->error(); }

    void set_loop(bool on) { m->native->set_loop(on); }
    bool loop() const { return m->native->loop(); }
    bool set_rate(const F& rate) { return m->native->set_rate(d(rate)); }
    F rate() const { return w(m->native->rate()); }
    void set_preserve_pitch(bool on) { m->native->set_preserve_pitch(on); }
    bool preserve_pitch() const { return m->native->preserve_pitch(); }
    void set_volume(const F& volume) { m->native->set_volume(static_cast<float>(std::max(0.0, d(volume)))); }
    F volume() const { return w(m->native->volume()); }
    void set_muted(bool on) { m->native->set_muted(on); }
    bool muted() const { return m->native->muted(); }

    event_list poll() { return event_list(m->native->poll_events()); }

    template <typename Fn>
    void set_event_callback(Fn callback) {
        state* st = m.get();
        std::shared_ptr<listener> l = std::make_shared<listener>();
        l->run = [l_raw = l.get(), callback](const fv::PlayerEvent& e) mutable {
            event ev(e);
            try {
                call_with_event(callback, ev);
            } catch (const std::exception& x) {
                l_raw->fail_with(x.what());
            } catch (...) {
                l_raw->fail_with("an exception was thrown");
            }
        };
        {
            std::lock_guard<std::recursive_mutex> lock(st->call_mu);
            st->hold.hold();
            st->current = l;
        }
        std::weak_ptr<listener> weak = l;
        m->native->set_event_callback([st, weak](const fv::PlayerEvent& e) {
            std::lock_guard<std::recursive_mutex> lock(st->call_mu);
            std::shared_ptr<listener> live = weak.lock();
            if (!live || st->current != live || live->failed) return;
            live->run(e);
        });
    }

    void clear_event_callback() {
        m->native->set_event_callback(nullptr);
        std::lock_guard<std::recursive_mutex> lock(m->call_mu);
        m->current.reset();
        m->hold.release();
    }

    std::string callback_error() const {
        std::lock_guard<std::recursive_mutex> lock(m->call_mu);
        return m->current ? m->current->error : std::string();
    }

    frame next_frame() {
        const fv::VideoFrame* f = nullptr;
        std::uint64_t serial = m->frame_serial;
        if (!m->native->current_frame(f, serial)) return frame();
        m->frame_serial = serial;
        if (!f || f->empty()) return frame();
        return frame(*f, serial);
    }

    frame current_frame() {
        const fv::VideoFrame* f = nullptr;
        std::uint64_t serial = m->frame_serial;
        m->native->current_frame(f, serial);
        m->frame_serial = serial;
        if (!f || f->empty()) return frame();
        return frame(*f, serial);
    }

    picture next_picture() {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint64_t serial = m->rgba_serial;
        if (!m->native->frame_rgba(m->rgba, width, height, serial)) return picture();
        m->rgba_serial = serial;
        if (m->rgba.empty()) return picture();
        return picture(m->rgba, width, height, serial, m->native->frame_time());
    }

    long long frame_serial() const { return static_cast<long long>(m->native->frame_serial()); }
    F frame_time() const { return w(m->native->frame_time()); }
    F next_frame_delay() const { return w(m->native->next_frame_delay()); }

    long long next_frame_delay_ms(long long fallback) const {
        const double delay = m->native->next_frame_delay();
        if (delay < 0.0) return fallback;
        return static_cast<long long>(delay * 1000.0);
    }

    stats statistics() const { return stats(m->native->stats()); }

    void attach(const walnut_rt::audio::mixer& mix) {
        std::shared_ptr<walnut_rt::audio::mixer_state> s = mix.shared_state();
        if (!s) return;
        m->native->attach(s->mixer);
        m->mixer = std::move(s);
    }

    void detach() {
        m->native->detach();
        m->mixer.reset();
    }

    bool attached() const { return static_cast<bool>(m->mixer); }

    bool wait(long long timeout_ms) const {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms < 0 ? 0 : timeout_ms);
        while (true) {
            const fv::PlayerState s = m->native->state();
            if (s == fv::PlayerState::ended) return true;
            if (s == fv::PlayerState::closed || s == fv::PlayerState::error) return false;
            if (s == fv::PlayerState::paused && !m->native->is_seeking()) return false;
            if (timeout_ms >= 0 && std::chrono::steady_clock::now() >= until) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

private:
    struct listener {
        std::function<void(const fv::PlayerEvent&)> run;
        std::string error;
        bool failed = false;

        void fail_with(const char* message) {
            if (error.empty()) error = message;
            failed = true;
        }
    };

    struct state {
        walnut_rt::audio::gc_hold hold;
        std::shared_ptr<walnut_rt::audio::mixer_state> mixer;
        std::unique_ptr<fv::MediaPlayer> native{ new fv::MediaPlayer() };
        mutable std::recursive_mutex call_mu;
        std::shared_ptr<listener> current;
        std::uint64_t frame_serial = 0;
        std::uint64_t rgba_serial = 0;
        std::vector<std::uint8_t> rgba;

        ~state() {
            native->set_event_callback(nullptr);
            native->close();
            native.reset();
            current.reset();
            hold.release();
        }
    };

    void reset_serials() {
        m->frame_serial = 0;
        m->rgba_serial = 0;
        m->rgba.clear();
    }

    std::shared_ptr<state> m;
};

} // namespace video
} // namespace walnut_rt

#endif // WALNUT_VIDEO_HPP
