#ifndef WALNUT_AUDIO_HPP
#define WALNUT_AUDIO_HPP

#include "walnut_runtime.hpp"
#include "fizmo/../../Source/Audio/audio.hpp"
#include "fizmo/../../Source/Audio/presets.hpp"
#include "fizmo/../../Source/Audio/stretch.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace walnut_rt {
namespace audio {

namespace fa = fizmo::audio;

using F = walnut_rt::f128;

inline double d(const F& v) { return walnut_rt::to_native<double>(v); }
inline float f(const F& v) { return static_cast<float>(walnut_rt::to_native<double>(v)); }
inline F w(double v) {
    if (!std::isfinite(v)) return walnut_rt::from_native<F>(v);
    if (v == 0.0) return walnut_rt::from_native<F>(0.0);
    char text[40];
    for (int digits = 1; digits <= 17; ++digits) {
        std::snprintf(text, sizeof(text), "%.*g", digits, v);
        if (std::strtod(text, nullptr) == v) break;
    }
    return F(std::string(text));
}

inline F w(float v) {
    if (!std::isfinite(v)) return walnut_rt::from_native<F>(static_cast<double>(v));
    if (v == 0.0f) return walnut_rt::from_native<F>(0.0);
    char text[32];
    for (int digits = 1; digits <= 9; ++digits) {
        std::snprintf(text, sizeof(text), "%.*g", digits, static_cast<double>(v));
        if (std::strtof(text, nullptr) == v) break;
    }
    return F(std::string(text));
}

inline std::size_t count_of(long long v) { return v <= 0 ? 0u : static_cast<std::size_t>(v); }
inline std::uint32_t rate_of(long long v) { return v <= 0 ? 0u : static_cast<std::uint32_t>(std::min<long long>(v, 1536000)); }

inline std::string& last_error() {
    thread_local std::string e;
    return e;
}

inline void set_error(const std::string& e) { last_error() = e; }

template <typename Fn>
struct callable_traits : callable_traits<decltype(&Fn::operator())> {};

template <typename C, typename R, typename... A>
struct callable_traits<R (C::*)(A...) const> {
    using args = std::tuple<A...>;
};

template <typename C, typename R, typename... A>
struct callable_traits<R (C::*)(A...)> {
    using args = std::tuple<A...>;
};

template <typename R, typename... A>
struct callable_traits<std::function<R(A...)>> {
    using args = std::tuple<A...>;
};

template <typename R, typename... A>
struct callable_traits<R (*)(A...)> {
    using args = std::tuple<A...>;
};

template <typename Fn, std::size_t I>
using arg_of = std::decay_t<std::tuple_element_t<I, typename callable_traits<std::decay_t<Fn>>::args>>;

class gc_hold {
public:
    gc_hold() = default;
    gc_hold(const gc_hold&) = delete;
    gc_hold& operator=(const gc_hold&) = delete;

    void hold() {
        if (!m_scope) m_scope.reset(new safety::thread_scope());
    }

    void release() { m_scope.reset(); }

private:
    std::unique_ptr<safety::thread_scope> m_scope;
};

inline fa::DeviceKind kind_of(long long k) {
    if (k == 1) return fa::DeviceKind::input;
    if (k == 2) return fa::DeviceKind::loopback;
    return fa::DeviceKind::output;
}

inline long long kind_number(fa::DeviceKind k) {
    if (k == fa::DeviceKind::input) return 1;
    if (k == fa::DeviceKind::loopback) return 2;
    return 0;
}

inline fa::ResampleQuality quality_of(long long q) {
    switch (q) {
        case 0: return fa::ResampleQuality::fast;
        case 1: return fa::ResampleQuality::medium;
        case 3: return fa::ResampleQuality::best;
        default: return fa::ResampleQuality::high;
    }
}

inline fa::FadeCurve curve_of(long long c) {
    switch (c) {
        case 1: return fa::FadeCurve::equal_power;
        case 2: return fa::FadeCurve::logarithmic;
        case 3: return fa::FadeCurve::exponential;
        case 4: return fa::FadeCurve::s_curve;
        default: return fa::FadeCurve::linear;
    }
}

inline fa::FadeShape shape_of(long long c) {
    switch (c) {
        case 1: return fa::FadeShape::equal_power;
        case 2: return fa::FadeShape::logarithmic;
        case 3: return fa::FadeShape::exponential;
        case 4: return fa::FadeShape::s_curve;
        default: return fa::FadeShape::linear;
    }
}

inline fa::Curve automation_curve_of(long long c) {
    switch (c) {
        case 1: return fa::Curve::hold;
        case 2: return fa::Curve::exponential;
        case 3: return fa::Curve::s_curve;
        case 4: return fa::Curve::bezier;
        default: return fa::Curve::linear;
    }
}

inline fa::AudioFileType file_type_of(long long t) {
    switch (t) {
        case 1: return fa::AudioFileType::wav;
        case 2: return fa::AudioFileType::flac;
        case 3: return fa::AudioFileType::mp3;
        case 4: return fa::AudioFileType::ogg_vorbis;
        case 5: return fa::AudioFileType::ogg_opus;
        case 6: return fa::AudioFileType::aac;
        case 7: return fa::AudioFileType::mp4;
        case 8: return fa::AudioFileType::matroska;
        case 9: return fa::AudioFileType::ogg;
        default: return fa::AudioFileType::unknown;
    }
}

inline long long file_type_number(fa::AudioFileType t) {
    switch (t) {
        case fa::AudioFileType::wav: return 1;
        case fa::AudioFileType::flac: return 2;
        case fa::AudioFileType::mp3: return 3;
        case fa::AudioFileType::ogg_vorbis: return 4;
        case fa::AudioFileType::ogg_opus: return 5;
        case fa::AudioFileType::aac: return 6;
        case fa::AudioFileType::mp4: return 7;
        case fa::AudioFileType::matroska: return 8;
        case fa::AudioFileType::ogg: return 9;
        default: return 0;
    }
}

inline fa::SampleFormat sample_format_of(long long s) {
    switch (s) {
        case 0: return fa::SampleFormat::u8;
        case 2: return fa::SampleFormat::s24;
        case 3: return fa::SampleFormat::s32;
        case 4: return fa::SampleFormat::f32;
        case 5: return fa::SampleFormat::f64;
        case 6: return fa::SampleFormat::alaw;
        case 7: return fa::SampleFormat::ulaw;
        default: return fa::SampleFormat::s16;
    }
}

inline fa::StretchQuality stretch_quality_of(long long q) {
    if (q == 0) return fa::StretchQuality::fast;
    if (q == 1) return fa::StretchQuality::normal;
    return fa::StretchQuality::high;
}

inline fa::StretchMode stretch_mode_of(long long m) {
    if (m == 1) return fa::StretchMode::phase_vocoder;
    if (m == 2) return fa::StretchMode::wsola;
    return fa::StretchMode::automatic;
}

struct api {};

class buffer;

class block {
public:
    block() = default;
    block(float* data, std::size_t frames, std::size_t channels, std::uint32_t rate, bool writable)
        : m_data(data), m_frames(frames), m_channels(channels), m_rate(rate), m_writable(writable) {}

    long long frames() const { return static_cast<long long>(m_frames); }
    long long channels() const { return static_cast<long long>(m_channels); }
    long long sample_rate() const { return static_cast<long long>(m_rate); }
    long long sample_count() const { return static_cast<long long>(m_frames * m_channels); }
    bool writable() const { return m_writable; }

    float get(long long frame, long long channel) const {
        check(frame, channel, "get()");
        return m_data[static_cast<std::size_t>(frame) * m_channels + static_cast<std::size_t>(channel)];
    }

    void set(long long frame, long long channel, float value) {
        check(frame, channel, "set()");
        writable_check("set()");
        m_data[static_cast<std::size_t>(frame) * m_channels + static_cast<std::size_t>(channel)] = value;
    }

    void set_wide(long long frame, long long channel, const F& value) { set(frame, channel, f(value)); }

    float get_index(long long index) const {
        if (index < 0 || static_cast<std::size_t>(index) >= m_frames * m_channels) fail("AudioBlock::sample(): index " + std::to_string(index) + " is outside the block's " + std::to_string(m_frames * m_channels) + " samples");
        return m_data[static_cast<std::size_t>(index)];
    }

    void set_index(long long index, float value) {
        if (index < 0 || static_cast<std::size_t>(index) >= m_frames * m_channels) fail("AudioBlock::set_sample(): index " + std::to_string(index) + " is outside the block's " + std::to_string(m_frames * m_channels) + " samples");
        writable_check("set_sample()");
        m_data[static_cast<std::size_t>(index)] = value;
    }

    void set_index_wide(long long index, const F& value) { set_index(index, f(value)); }

    void fill(float value) {
        writable_check("fill()");
        std::fill(m_data, m_data + m_frames * m_channels, value);
    }

    void fill_wide(const F& value) { fill(f(value)); }

    void apply_gain(const F& gain) {
        writable_check("apply_gain()");
        const float g = f(gain);
        for (std::size_t i = 0; i < m_frames * m_channels; ++i) m_data[i] *= g;
    }

    float peak() const {
        float p = 0.0f;
        for (std::size_t i = 0; i < m_frames * m_channels; ++i) p = std::max(p, std::fabs(m_data[i]));
        return p;
    }

    float rms() const {
        const std::size_t n = m_frames * m_channels;
        if (!n) return 0.0f;
        double sum = 0.0;
        for (std::size_t i = 0; i < n; ++i) sum += static_cast<double>(m_data[i]) * m_data[i];
        return static_cast<float>(std::sqrt(sum / static_cast<double>(n)));
    }

    long long copy_from(const buffer& source, long long start_frame, const F& gain, bool add);

    buffer to_buffer() const;

    float* raw() const { return m_data; }

private:
    void check(long long frame, long long channel, const char* what) const {
        if (frame < 0 || static_cast<std::size_t>(frame) >= m_frames || channel < 0 || static_cast<std::size_t>(channel) >= m_channels) {
            fail(std::string("AudioBlock::") + what + ": frame " + std::to_string(frame) + ", channel " + std::to_string(channel) + " is outside the block (" + std::to_string(m_frames) + " frames, " + std::to_string(m_channels) + " channels)");
        }
    }

    void writable_check(const char* what) const {
        if (!m_writable) fail(std::string("AudioBlock::") + what + ": a capture block can only be read");
    }

    float* m_data = nullptr;
    std::size_t m_frames = 0;
    std::size_t m_channels = 0;
    std::uint32_t m_rate = 0;
    bool m_writable = false;
};

template <typename Fn>
inline void call_with_block(Fn& fn, block& b) {
    using A = arg_of<Fn, 0>;
    if constexpr (std::is_same_v<A, block>) {
        fn(b);
    } else {
        A wrapped(walnut_rt::native_tag{}, b);
        fn(wrapped);
    }
}

class buffer {
public:
    buffer() = default;
    buffer(long long channels, long long sample_rate) : b(count_of(channels), rate_of(sample_rate), 0) {}
    buffer(long long channels, long long sample_rate, long long frames) : b(count_of(channels), rate_of(sample_rate), count_of(frames)) {}
    explicit buffer(fa::AudioBuffer other) : b(std::move(other)) {}

    static buffer from_samples(const std::vector<float>& samples, long long channels, long long sample_rate) {
        const std::size_t c = count_of(channels);
        std::vector<float> data = samples;
        if (c) data.resize(data.size() / c * c);
        return buffer(fa::AudioBuffer(c, rate_of(sample_rate), std::move(data)));
    }

    static buffer tone(const F& frequency, const F& seconds, long long sample_rate, long long channels, const F& amplitude, long long shape) {
        const std::uint32_t rate = rate_of(sample_rate);
        const std::size_t c = count_of(channels);
        const double hz = d(frequency);
        const double secs = std::max(0.0, d(seconds));
        const float amp = f(amplitude);
        const std::size_t frames = static_cast<std::size_t>(std::llround(secs * rate));
        fa::AudioBuffer out(c, rate, frames);
        const double two_pi = 6.283185307179586476925286766559;
        double phase = 0.0;
        const double step = rate ? hz / static_cast<double>(rate) : 0.0;
        std::uint32_t seed = 22222u;
        for (std::size_t i = 0; i < frames; ++i) {
            double v = 0.0;
            switch (shape) {
                case 1: v = phase < 0.5 ? 1.0 : -1.0; break;
                case 2: v = 2.0 * phase - 1.0; break;
                case 3: v = phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase; break;
                case 4: seed = seed * 1664525u + 1013904223u; v = static_cast<double>(seed >> 8) / 8388608.0 - 1.0; break;
                default: v = std::sin(two_pi * phase); break;
            }
            phase += step;
            phase -= std::floor(phase);
            const float s = static_cast<float>(v) * amp;
            for (std::size_t ch = 0; ch < c; ++ch) out.at(i, ch) = s;
        }
        return buffer(std::move(out));
    }

    static buffer load(const std::string& path) {
        fa::AudioBuffer out;
        std::string error;
        set_error(std::string());
        if (!fa::load_audio(path, out, &error)) {
            set_error(error.empty() ? std::string("unknown error") : error);
            return buffer();
        }
        return buffer(std::move(out));
    }

    static std::string load_error() { return last_error(); }

    std::string save(const std::string& path, long long type, long long wav_format, bool dither, long long flac_compression, long long flac_bits, long long mp3_bitrate, bool mp3_vbr, long long mp3_vbr_quality, long long mp3_quality, const std::vector<std::string>& tag_keys, const std::vector<std::string>& tag_values) const {
        if (b.channels() == 0 || b.sample_rate() == 0) return "the buffer has no channels or no sample rate";
        fa::SaveOptions o;
        o.type = file_type_of(type);
        o.wav_format = sample_format_of(wav_format);
        o.dither = dither;
        o.flac_compression = static_cast<int>(std::max(0LL, std::min(8LL, flac_compression)));
        o.flac_bits = static_cast<unsigned>(std::max(4LL, std::min(32LL, flac_bits)));
        o.mp3_bitrate_kbps = static_cast<std::uint32_t>(std::max(8LL, std::min(320LL, mp3_bitrate)));
        o.mp3_vbr = mp3_vbr;
        o.mp3_vbr_quality = static_cast<int>(std::max(0LL, std::min(9LL, mp3_vbr_quality)));
        o.mp3_quality = static_cast<int>(std::max(0LL, std::min(9LL, mp3_quality)));
        for (std::size_t i = 0; i < tag_keys.size() && i < tag_values.size(); ++i) o.tags.emplace_back(tag_keys[i], tag_values[i]);
        std::string error;
        if (!fa::save_audio(path, b, o, &error)) return error.empty() ? std::string("unknown error") : error;
        return std::string();
    }

    long long channels() const { return static_cast<long long>(b.channels()); }
    long long sample_rate() const { return static_cast<long long>(b.sample_rate()); }
    long long frames() const { return static_cast<long long>(b.frames()); }
    long long sample_count() const { return static_cast<long long>(b.sample_count()); }
    F duration() const { return w(b.duration()); }
    bool empty() const { return b.empty(); }

    float get(long long frame, long long channel) const { return b.at(static_cast<std::size_t>(frame), static_cast<std::size_t>(channel)); }
    void set(long long frame, long long channel, float value) { b.at(static_cast<std::size_t>(frame), static_cast<std::size_t>(channel)) = value; }
    void set_wide(long long frame, long long channel, const F& value) { set(frame, channel, f(value)); }

    std::vector<float> samples() const { return b.samples(); }
    void set_samples(const std::vector<float>& values) {
        std::vector<float> v = values;
        if (b.channels()) v.resize(v.size() / b.channels() * b.channels());
        b.samples() = std::move(v);
    }
    std::vector<float> channel(long long index) const { return b.channel(static_cast<std::size_t>(index)); }
    void set_channel(long long index, const std::vector<float>& values) { b.set_channel(static_cast<std::size_t>(index), values); }

    void set_sample_rate(long long rate) { b.set_sample_rate(rate_of(rate)); }
    void resize(long long frames) { b.resize(count_of(frames)); }
    void clear() { b.clear(); }
    void silence() { b.silence(); }

    bool append(const buffer& other) { return b.append(other.b); }
    bool insert(long long at, const buffer& other) { return b.insert(count_of(at), other.b); }
    void erase(long long start, long long count) { b.erase(count_of(start), count_of(count)); }
    bool mix_in(const buffer& other, long long at, const F& gain) { return b.mix_in(other.b, count_of(at), f(gain)); }

    buffer slice(long long start, long long count) const { return buffer(b.slice(count_of(start), count_of(count))); }
    buffer with_channels(long long channels) const { return buffer(b.with_channels(count_of(channels))); }
    buffer resampled(long long rate, long long quality) const { return buffer(b.resampled(rate_of(rate), quality_of(quality))); }
    buffer converted(long long channels, long long rate, long long quality) const { return buffer(b.converted(count_of(channels), rate_of(rate), quality_of(quality))); }

    long long frame_at(const F& seconds) const { return static_cast<long long>(b.frame_at(d(seconds))); }
    F seconds_at(long long frame) const { return w(b.seconds_at(count_of(frame))); }

    float peak() const { return b.peak(); }
    float rms() const { return b.rms(); }
    F peak_db() const { return w(fa::peak_db(b)); }
    F true_peak_db() const { return w(fa::true_peak_db(b)); }
    void apply_gain(const F& gain) { b.apply_gain(f(gain)); }
    void apply_gain_db(const F& db) { b.apply_gain(fa::db_to_gain(f(db))); }
    void normalize(const F& target) { b.normalize(f(target)); }
    void normalize_db(const F& target_db) { fa::normalize_peak(b, f(target_db)); }
    void clamp() { b.clamp(); }

    void fade(long long start, long long count, const F& from, const F& to, long long curve) { fa::fade(b, count_of(start), count_of(count), f(from), f(to), curve_of(curve)); }
    void fade_in(const F& seconds, long long curve) { fa::fade_in(b, d(seconds), curve_of(curve)); }
    void fade_out(const F& seconds, long long curve) { fa::fade_out(b, d(seconds), curve_of(curve)); }
    void reverse() { fa::reverse(b); }
    void invert_polarity() { fa::invert_polarity(b); }
    void remove_dc() { fa::remove_dc(b); }
    void swap_channels(long long first, long long second) { fa::swap_channels(b, count_of(first), count_of(second)); }
    void pad(const F& before, const F& after) { fa::pad(b, std::max(0.0, d(before)), std::max(0.0, d(after))); }
    void trim_silence(const F& threshold_db, const F& keep_ms) { fa::trim_silence(b, f(threshold_db), std::max(0.0, d(keep_ms))); }

    std::vector<std::int64_t> silence_ranges(const F& threshold_db, const F& min_seconds) const {
        std::vector<std::int64_t> out;
        for (const fa::FrameRange& r : fa::detect_silence(b, f(threshold_db), std::max(0.0, d(min_seconds)))) {
            out.push_back(static_cast<std::int64_t>(r.first));
            out.push_back(static_cast<std::int64_t>(r.second));
        }
        return out;
    }

    void gain_envelope(const std::vector<double>& times, const std::vector<double>& dbs) {
        std::vector<std::pair<double, float>> points;
        for (std::size_t i = 0; i < times.size() && i < dbs.size(); ++i) points.emplace_back(times[i], static_cast<float>(dbs[i]));
        fa::apply_gain_envelope(b, std::move(points));
    }

    buffer to_mono() const { return b.channels() == 2 ? buffer(fa::stereo_to_mono(b)) : buffer(b.with_channels(1)); }
    buffer to_stereo() const { return b.channels() == 1 ? buffer(fa::mono_to_stereo(b)) : buffer(b.with_channels(2)); }

    buffer time_stretched(const F& tempo, long long quality, long long mode) const { return buffer(fa::time_stretch(b, d(tempo), stretch_quality_of(quality), stretch_mode_of(mode))); }
    buffer pitch_changed(const F& semitones, long long quality, long long mode) const { return buffer(fa::change_pitch(b, d(semitones), stretch_quality_of(quality), stretch_mode_of(mode))); }
    buffer tempo_and_pitch_changed(const F& tempo, const F& semitones, long long quality, long long mode) const { return buffer(fa::change_tempo_and_pitch(b, d(tempo), d(semitones), stretch_quality_of(quality), stretch_mode_of(mode))); }
    buffer speed_changed(const F& factor, long long quality) const { return buffer(fa::change_speed(b, d(factor), quality_of(quality))); }

    F detect_pitch(const F& min_hz, const F& max_hz) const {
        if (b.empty() || b.sample_rate() == 0) return w(0.0);
        const std::vector<float> mono = b.channels() == 1 ? b.samples() : b.with_channels(1).samples();
        float confidence = 0.0f;
        const float hz = fa::detect_pitch(mono.data(), mono.size(), static_cast<float>(b.sample_rate()), f(min_hz), f(max_hz), &confidence);
        return w(hz > 0.0f ? hz : 0.0f);
    }

    std::vector<float> spectrum(long long bands, const F& min_hz, const F& max_hz, long long fft_size) const {
        const std::size_t n = std::max<std::size_t>(1, count_of(bands));
        std::size_t size = fa::next_power_of_two(std::max<std::size_t>(fa::SpectrumAnalyzer::min_fft_size, count_of(fft_size)));
        size = std::min(size, fa::SpectrumAnalyzer::max_fft_size);
        if (b.empty() || b.channels() == 0) return std::vector<float>(n, -120.0f);
        fa::SpectrumAnalyzer an(size);
        an.prepare(b.sample_rate(), b.channels());
        an.set_smoothing(0.0f);
        an.push(b.data(), b.frames());
        an.update();
        return an.bands(n, f(min_hz), f(max_hz));
    }

    fa::AudioBuffer b;
};

inline long long block::copy_from(const buffer& source, long long start_frame, const F& gain, bool add) {
    writable_check(add ? "add_from()" : "copy_from()");
    const fa::AudioBuffer& s = source.b;
    if (start_frame < 0 || s.channels() == 0) return 0;
    const std::size_t start = static_cast<std::size_t>(start_frame);
    if (start >= s.frames()) return 0;
    const std::size_t n = std::min(m_frames, s.frames() - start);
    const float g = f(gain);
    for (std::size_t i = 0; i < n; ++i) {
        const float* in = s.frame(start + i);
        float* out = m_data + i * m_channels;
        for (std::size_t c = 0; c < m_channels; ++c) {
            const float v = in[s.channels() == 1 ? 0 : (c < s.channels() ? c : s.channels() - 1)] * g;
            out[c] = add ? out[c] + v : v;
        }
    }
    return static_cast<long long>(n);
}

inline buffer block::to_buffer() const {
    return buffer(fa::AudioBuffer(m_channels, m_rate, std::vector<float>(m_data, m_data + m_frames * m_channels)));
}

inline buffer crossfade(const buffer& a, const buffer& b, const F& seconds, long long curve) { return buffer(fa::crossfade(a.b, b.b, std::max(0.0, d(seconds)), curve_of(curve))); }
inline buffer mix(const buffer& a, const buffer& b, const F& gain) { return buffer(fa::mix(a.b, b.b, f(gain))); }
inline F db_to_gain(const F& db) { return w(fa::db_to_gain(f(db))); }
inline F gain_to_db(const F& gain) { return w(fa::gain_to_db(f(gain))); }

class loudness {
public:
    loudness() = default;
    explicit loudness(const fa::LoudnessResult& r) : m(r) {}
    static loudness measure(const buffer& b) { return loudness(fa::measure_loudness(b.b)); }
    F integrated() const { return w(m.integrated); }
    F range() const { return w(m.range); }
    F true_peak_db() const { return w(m.true_peak_db); }
    F sample_peak_db() const { return w(m.sample_peak_db); }
    F max_momentary() const { return w(m.max_momentary); }
    F max_short_term() const { return w(m.max_short_term); }

private:
    fa::LoudnessResult m;
};

inline void normalize_loudness(buffer& b, const F& target_lufs, const F& max_true_peak_db) { fa::normalize_loudness(b.b, f(target_lufs), f(max_true_peak_db)); }

class file_info {
public:
    file_info() = default;

    static file_info read(const std::string& path) {
        file_info out;
        std::string error;
        set_error(std::string());
        if (!fa::read_audio_info(path, out.m, &error)) {
            set_error(error.empty() ? std::string("unknown error") : error);
            out.m_ok = false;
            return out;
        }
        out.m_ok = true;
        return out;
    }

    bool ok() const { return m_ok; }
    long long type() const { return file_type_number(m.type); }
    std::string type_name() const { return fa::audio_type_name(m.type); }
    long long sample_rate() const { return static_cast<long long>(m.sample_rate); }
    long long channels() const { return static_cast<long long>(m.channels); }
    long long frames() const { return static_cast<long long>(m.frames); }
    F duration() const { return w(m.duration); }
    long long bitrate_kbps() const { return static_cast<long long>(m.bitrate_kbps); }
    std::string tag(const std::string& key) const { return m.tag(key); }

    std::vector<std::string> tag_keys() const {
        std::vector<std::string> out;
        for (const auto& t : m.tags) out.push_back(t.first);
        return out;
    }

private:
    fa::AudioFileInfo m;
    bool m_ok = false;
};

inline long long type_from_extension(const std::string& path) { return file_type_number(fa::audio_type_from_extension(path)); }
inline std::vector<std::string> readable_extensions() { return fa::readable_audio_extensions(); }
inline std::vector<std::string> writable_extensions() { return fa::writable_audio_extensions(); }

class device {
public:
    device() = default;
    explicit device(fa::DeviceInfo i) : m(std::move(i)) {}

    std::string id() const { return m.id; }
    std::string name() const { return m.name; }
    long long kind() const { return kind_number(m.kind); }
    bool is_default() const { return m.is_default; }
    long long sample_rate() const { return static_cast<long long>(m.sample_rate); }
    long long channels() const { return static_cast<long long>(m.channels); }
    std::string backend() const { return m.backend; }

private:
    fa::DeviceInfo m;
};

class device_list {
public:
    device_list() = default;

    static device_list list(long long kind) {
        device_list out;
        out.m = kind < 0 ? fa::devices() : fa::devices(kind_of(kind));
        return out;
    }

    static device_list default_of(long long kind) {
        device_list out;
        std::optional<fa::DeviceInfo> d = fa::default_device(kind_of(kind));
        if (d) out.m.push_back(*d);
        return out;
    }

    long long size() const { return static_cast<long long>(m.size()); }
    device at(long long i) const { return device(m[static_cast<std::size_t>(i)]); }

private:
    std::vector<fa::DeviceInfo> m;
};

inline std::string backend_name() { return fa::backend_name(); }
inline std::vector<std::string> available_backends() { return fa::available_backends(); }

inline std::string set_backend(const std::string& name) {
    std::string error;
    if (!fa::set_backend(name, &error)) return error.empty() ? std::string("unknown backend '") + name + "'" : error;
    return std::string();
}

inline fa::StreamConfig make_config(fa::DeviceKind kind, const std::string& device, long long rate, long long channels, long long latency_ms, const std::string& name) {
    fa::StreamConfig c;
    c.kind = kind;
    c.device = device;
    c.sample_rate = rate_of(rate);
    c.channels = static_cast<std::uint32_t>(std::min<long long>(std::max<long long>(channels, 0), 64));
    c.latency_ms = static_cast<std::uint32_t>(std::max<long long>(1, std::min<long long>(latency_ms, 2000)));
    c.name = name.empty() ? std::string("walnut") : name;
    return c;
}

class stream {
public:
    stream() : m_state(std::make_shared<state>()) {}

    template <typename Fn>
    static stream open_playback(const std::string& device, long long rate, long long channels, long long latency_ms, const std::string& name, Fn callback) {
        stream s;
        std::string error;
        set_error(std::string());
        state* st = s.m_state.get();
        const fa::StreamConfig c = make_config(fa::DeviceKind::output, device, rate, channels, latency_ms, name);
        st->hold.hold();
        const bool ok = st->native.open_playback(c, [st, callback](float* out, std::size_t frames) mutable {
            block b(out, frames, st->channels, st->rate, true);
            call_with_block(callback, b);
        }, &error);
        if (!ok) {
            st->hold.release();
            set_error(error.empty() ? std::string("unknown error") : error);
            return s;
        }
        st->rate = st->native.sample_rate();
        st->channels = st->native.channels();
        return s;
    }

    template <typename Fn>
    static stream open_capture(long long kind, const std::string& device, long long rate, long long channels, long long latency_ms, const std::string& name, Fn callback) {
        stream s;
        std::string error;
        set_error(std::string());
        state* st = s.m_state.get();
        const fa::StreamConfig c = make_config(kind == 2 ? fa::DeviceKind::loopback : fa::DeviceKind::input, device, rate, channels, latency_ms, name);
        st->hold.hold();
        const bool ok = st->native.open_capture(c, [st, callback](const float* in, std::size_t frames) mutable {
            block b(const_cast<float*>(in), frames, st->channels, st->rate, false);
            call_with_block(callback, b);
        }, &error);
        if (!ok) {
            st->hold.release();
            set_error(error.empty() ? std::string("unknown error") : error);
            return s;
        }
        st->rate = st->native.sample_rate();
        st->channels = st->native.channels();
        return s;
    }

    static std::string open_error() { return last_error(); }

    std::string start() {
        std::string error;
        if (!m_state->native.is_open()) return "the stream isn't open";
        if (!m_state->native.start(&error)) return error.empty() ? std::string("unknown error") : error;
        return std::string();
    }

    void stop() { m_state->native.stop(); }

    void close() {
        m_state->native.close();
        m_state->hold.release();
    }

    bool is_open() const { return m_state->native.is_open(); }
    bool running() const { return m_state->native.running(); }
    bool failed() const { return m_state->native.failed(); }
    std::string error() const { return m_state->native.error(); }
    long long kind() const { return kind_number(m_state->native.kind()); }
    long long sample_rate() const { return static_cast<long long>(m_state->native.sample_rate()); }
    long long channels() const { return static_cast<long long>(m_state->native.channels()); }
    long long latency_frames() const { return static_cast<long long>(m_state->native.latency_frames()); }
    std::string device_name() const { return m_state->native.device_name(); }
    long long frames_processed() const { return static_cast<long long>(m_state->native.frames_processed()); }
    F seconds_processed() const { return w(m_state->native.seconds_processed()); }

private:
    struct state {
        gc_hold hold;
        fa::Stream native;
        std::size_t channels = 0;
        std::uint32_t rate = 0;

        ~state() { native.close(); }
    };

    std::shared_ptr<state> m_state;
};

class callback_effect : public fa::Effect {
public:
    callback_effect(std::string name, std::function<void(float*, std::size_t, std::size_t, std::uint32_t)> run) : m_name(std::move(name)), m_run(std::move(run)) {}

    const char* name() const noexcept override { return m_name.c_str(); }
    const char* type() const noexcept override { return "walnut_custom"; }
    void prepare(std::uint32_t sample_rate, std::size_t channels) override { set_format(sample_rate, channels); }
    void reset() noexcept override {}

    void process(float* interleaved, std::size_t frames) noexcept override {
        if (m_failed.load(std::memory_order_relaxed)) {
            std::memset(interleaved, 0, frames * (channels() ? channels() : 1) * sizeof(float));
            return;
        }
        try {
            m_run(interleaved, frames, channels() ? channels() : 1, sample_rate());
        } catch (const std::exception& e) {
            fail_with(e.what());
            std::memset(interleaved, 0, frames * (channels() ? channels() : 1) * sizeof(float));
        } catch (...) {
            fail_with("an exception was thrown");
            std::memset(interleaved, 0, frames * (channels() ? channels() : 1) * sizeof(float));
        }
    }

    std::string error() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_error;
    }

    void clear_error() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_error.clear();
        m_failed.store(false);
    }

private:
    void fail_with(const char* message) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_error.empty()) m_error = message;
        }
        m_failed.store(true);
    }

    std::string m_name;
    std::function<void(float*, std::size_t, std::size_t, std::uint32_t)> m_run;
    std::atomic<bool> m_failed{ false };
    mutable std::mutex m_mutex;
    std::string m_error;
};

class chain;

class effect {
public:
    effect() = default;
    explicit effect(std::shared_ptr<fa::Effect> e) : m(std::move(e)) {}

    static effect gain(const F& db) { return effect(std::make_shared<fa::Gain>(f(db))); }
    static effect filter(long long type, const F& frequency, const F& q, const F& gain_db, const F& slope_db) { return effect(std::make_shared<fa::Filter>(static_cast<fa::FilterType>(std::max(0LL, std::min(7LL, type))), f(frequency), f(q), f(gain_db), f(slope_db))); }
    static effect parametric_eq(long long bands) { return effect(std::make_shared<fa::ParametricEq>(std::max<std::size_t>(1, std::min(count_of(bands), fa::ParametricEq::max_bands)))); }
    static effect graphic_eq() { return effect(std::make_shared<fa::GraphicEq>()); }
    static effect dc_blocker() { return effect(std::make_shared<fa::DcBlocker>()); }
    static effect compressor(const F& threshold, const F& ratio, const F& attack, const F& release) { return effect(std::make_shared<fa::Compressor>(f(threshold), f(ratio), f(attack), f(release))); }
    static effect limiter(const F& ceiling, const F& release, const F& lookahead) { return effect(std::make_shared<fa::Limiter>(f(ceiling), f(release), f(lookahead))); }
    static effect noise_gate(const F& threshold, const F& attack, const F& hold, const F& release) { return effect(std::make_shared<fa::NoiseGate>(f(threshold), 6.0f, f(attack), f(hold), f(release))); }
    static effect expander(const F& threshold, const F& ratio) { return effect(std::make_shared<fa::Expander>(f(threshold), f(ratio))); }
    static effect de_esser(const F& frequency, const F& threshold, const F& amount) { return effect(std::make_shared<fa::DeEsser>(f(frequency), f(threshold), f(amount))); }
    static effect reverb() { return effect(std::make_shared<fa::Reverb>()); }
    static effect delay() { return effect(std::make_shared<fa::Delay>()); }
    static effect distortion() { return effect(std::make_shared<fa::Distortion>()); }
    static effect bitcrusher() { return effect(std::make_shared<fa::Bitcrusher>()); }
    static effect chorus() { return effect(std::make_shared<fa::Chorus>()); }
    static effect flanger() { return effect(std::make_shared<fa::Flanger>()); }
    static effect phaser() { return effect(std::make_shared<fa::Phaser>()); }
    static effect tremolo() { return effect(std::make_shared<fa::Tremolo>()); }
    static effect vibrato() { return effect(std::make_shared<fa::Vibrato>()); }
    static effect autopan() { return effect(std::make_shared<fa::AutoPan>()); }
    static effect pitch_shifter(const F& semitones) { return effect(std::make_shared<fa::PitchShifter>(f(semitones))); }
    static effect formant_shifter(const F& semitones) { return effect(std::make_shared<fa::FormantShifter>(f(semitones))); }
    static effect autotune() { return effect(std::make_shared<fa::AutoTune>()); }
    static effect voice_changer(long long preset) { return effect(std::make_shared<fa::VoiceChanger>(static_cast<fa::VoicePreset>(std::max(0LL, std::min<long long>(preset, static_cast<long long>(fa::voice_preset_count()) - 1))))); }
    static effect voice_eq() { return effect(std::make_shared<fa::VoiceEQ>()); }
    static effect ring_modulator() { return effect(std::make_shared<fa::RingModulator>()); }
    static effect vocoder() { return effect(std::make_shared<fa::Vocoder>()); }
    static effect noise_reduction() { return effect(std::make_shared<fa::NoiseReduction>()); }
    static effect hum_remover(const F& frequency) { return effect(std::make_shared<fa::HumRemover>(f(frequency))); }
    static effect click_remover() { return effect(std::make_shared<fa::ClickRemover>()); }

    static effect create(const std::string& type) { return effect(fa::create_effect(type)); }
    static std::vector<std::string> types() {
        std::vector<std::string> out;
        for (const fa::EffectTypeInfo& i : fa::effect_types()) out.push_back(i.type);
        return out;
    }

    template <typename Fn>
    static effect custom(const std::string& name, Fn fn) {
        auto run = [fn](float* data, std::size_t frames, std::size_t channels, std::uint32_t rate) mutable {
            block b(data, frames, channels, rate, true);
            call_with_block(fn, b);
        };
        return effect(std::make_shared<callback_effect>(name.empty() ? std::string("custom") : name, std::move(run)));
    }

    static effect from_json(const std::string& json) {
        std::string error;
        set_error(std::string());
        std::shared_ptr<fa::Effect> e = fa::load_effect(json, &error);
        if (!e) set_error(error.empty() ? std::string("the text isn't a saved effect") : error);
        return effect(std::move(e));
    }

    static std::string json_error() { return last_error(); }

    bool valid() const { return m != nullptr; }
    std::string name() const { return m ? std::string(m->name()) : std::string(); }
    std::string type() const { return m ? std::string(m->type()) : std::string(); }

    bool has(const std::string& p) const { return m && m->parameter(p) != nullptr; }
    bool set(const std::string& p, const F& value) { return m && m->set(p, f(value)); }
    F get(const std::string& p) const { return w(m ? m->get(p) : 0.0f); }
    F minimum(const std::string& p) const { const fa::Parameter* q = m ? static_cast<const fa::Effect&>(*m).parameter(p) : nullptr; return w(q ? q->minimum() : 0.0f); }
    F maximum(const std::string& p) const { const fa::Parameter* q = m ? static_cast<const fa::Effect&>(*m).parameter(p) : nullptr; return w(q ? q->maximum() : 0.0f); }
    std::string unit(const std::string& p) const { const fa::Parameter* q = m ? static_cast<const fa::Effect&>(*m).parameter(p) : nullptr; return q ? std::string(q->unit()) : std::string(); }

    std::vector<std::string> parameter_names() const {
        std::vector<std::string> out;
        if (!m) return out;
        for (const fa::Parameter* p : static_cast<const fa::Effect&>(*m).parameters()) out.push_back(p->name());
        return out;
    }

    void set_enabled(bool on) { if (m) m->set_enabled(on); }
    bool enabled() const { return m && m->enabled(); }
    void reset() { if (m) m->reset(); }
    long long latency_frames() const { return m ? static_cast<long long>(m->latency_frames()) : 0; }
    long long tail_frames() const { return m ? static_cast<long long>(m->tail_frames()) : 0; }

    buffer process(const buffer& in, bool keep_tail) const {
        if (!m) return in;
        return buffer(fa::applied(*m, in.b, keep_tail));
    }

    void apply(buffer& b, bool keep_tail) const {
        if (m) fa::apply(*m, b.b, keep_tail);
    }

    bool set_band(long long index, long long type, const F& frequency, const F& q, const F& gain_db, bool enabled) {
        auto* eq = dynamic_cast<fa::ParametricEq*>(m.get());
        if (!eq || index < 0 || static_cast<std::size_t>(index) >= eq->band_count()) return false;
        eq->set_band(static_cast<std::size_t>(index), static_cast<fa::FilterType>(std::max(0LL, std::min(7LL, type))), f(frequency), f(q), f(gain_db), enabled);
        return true;
    }

    long long band_count() const {
        if (auto* eq = dynamic_cast<fa::ParametricEq*>(m.get())) return static_cast<long long>(eq->band_count());
        if (dynamic_cast<fa::GraphicEq*>(m.get())) return static_cast<long long>(fa::GraphicEq::band_count);
        return 0;
    }

    bool set_graphic_band(long long index, const F& gain_db) {
        auto* eq = dynamic_cast<fa::GraphicEq*>(m.get());
        if (!eq || index < 0 || static_cast<std::size_t>(index) >= fa::GraphicEq::band_count) return false;
        eq->band(static_cast<std::size_t>(index)).set(f(gain_db));
        return true;
    }

    std::string learn_noise(const buffer& noise) {
        auto* nr = dynamic_cast<fa::NoiseReduction*>(m.get());
        if (!nr) return "learn_noise() only works on a noise_reduction effect";
        if (!nr->learn_profile(noise.b)) return "the noise sample is too short or empty";
        return std::string();
    }

    F detected_pitch() const {
        if (auto* p = dynamic_cast<fa::PitchShifter*>(m.get())) return w(p->detected_hz());
        if (auto* a = dynamic_cast<fa::AutoTune*>(m.get())) return w(a->detected_hz());
        return w(0.0);
    }

    F gain_reduction_db() const {
        if (auto* c = dynamic_cast<fa::Compressor*>(m.get())) return w(c->gain_reduction_db());
        if (auto* l = dynamic_cast<fa::Limiter*>(m.get())) return w(l->gain_reduction_db());
        if (auto* g = dynamic_cast<fa::NoiseGate*>(m.get())) return w(g->gain_reduction_db());
        if (auto* x = dynamic_cast<fa::Expander*>(m.get())) return w(x->gain_reduction_db());
        if (auto* s = dynamic_cast<fa::DeEsser*>(m.get())) return w(s->gain_reduction_db());
        return w(0.0);
    }

    std::string error() const {
        if (auto* c = dynamic_cast<callback_effect*>(m.get())) return c->error();
        return std::string();
    }

    void clear_error() {
        if (auto* c = dynamic_cast<callback_effect*>(m.get())) c->clear_error();
    }

    effect clone() const {
        if (!m) return effect();
        if (dynamic_cast<callback_effect*>(m.get())) return effect(m);
        std::shared_ptr<fa::Effect> c = fa::clone_effect(*m);
        return effect(c ? c : m);
    }

    std::string to_json() const { return m ? fa::save_effect(*m) : std::string(); }

    std::shared_ptr<fa::Effect> m;
};

class chain {
public:
    chain() : m(std::make_shared<fa::EffectChain>()) {}
    explicit chain(std::shared_ptr<fa::EffectChain> c) : m(c ? std::move(c) : std::make_shared<fa::EffectChain>()) {}

    static chain voice_preset(long long preset) {
        return chain(fa::builtin_voice_preset(static_cast<fa::VoicePreset>(std::max(0LL, std::min<long long>(preset, static_cast<long long>(fa::voice_preset_count()) - 1)))));
    }

    static chain voice_preset_named(const std::string& name) { return chain(fa::builtin_voice_preset(name)); }
    static std::vector<std::string> voice_preset_names() { return fa::builtin_voice_preset_names(); }

    static chain load_preset(const std::string& path) {
        set_error(std::string());
        auto c = std::make_shared<fa::EffectChain>();
        std::string error;
        if (!fa::load_preset_file(path, *c, nullptr, &error)) {
            set_error(error.empty() ? std::string("unknown error") : error);
        }
        return chain(c);
    }

    static chain from_json(const std::string& json) {
        set_error(std::string());
        auto c = std::make_shared<fa::EffectChain>();
        std::string error;
        if (!fa::load_preset(json, *c, nullptr, &error)) set_error(error.empty() ? std::string("the text isn't a saved preset") : error);
        return chain(c);
    }

    static std::string load_error() { return last_error(); }

    std::string save_preset(const std::string& path, const std::string& name, const std::string& author, const std::string& description) const {
        fa::PresetInfo info;
        info.name = name;
        info.author = author;
        info.description = description;
        std::string error;
        if (!fa::save_preset_file(path, *m, info, &error)) return error.empty() ? std::string("unknown error") : error;
        return std::string();
    }

    std::string to_json(const std::string& name) const {
        fa::PresetInfo info;
        info.name = name;
        return fa::save_preset(*m, info);
    }

    void add(const effect& e) { if (e.m) m->add(e.m); }
    bool insert(long long index, const effect& e) { return e.m && index >= 0 && m->insert(static_cast<std::size_t>(index), e.m); }
    bool remove(long long index) { return index >= 0 && m->remove(static_cast<std::size_t>(index)); }
    bool move_effect(long long from, long long to) { return from >= 0 && to >= 0 && m->move(static_cast<std::size_t>(from), static_cast<std::size_t>(to)); }
    void clear() { m->clear(); }
    long long size() const { return static_cast<long long>(m->size()); }
    effect at(long long index) const { return effect(index < 0 ? nullptr : m->at(static_cast<std::size_t>(index))); }
    void reset() { m->reset(); }
    void set_enabled(bool on) { m->set_enabled(on); }
    bool enabled() const { return m->enabled(); }
    long long latency_frames() const { return static_cast<long long>(m->latency_frames()); }
    long long tail_frames() const { return static_cast<long long>(m->tail_frames()); }
    effect as_effect() const { return effect(m); }

    buffer process(const buffer& in, bool keep_tail) const { return buffer(fa::applied(*m, in.b, keep_tail)); }
    void apply(buffer& b, bool keep_tail) const { fa::apply(*m, b.b, keep_tail); }

    std::shared_ptr<fa::EffectChain> m;
};

class ring {
public:
    void configure(std::size_t frames, std::size_t channels) {
        m_channels = channels ? channels : 1;
        m_capacity = std::max<std::size_t>(frames, 64);
        m_data.assign(m_capacity * m_channels, 0.0f);
        m_read.store(0);
        m_write.store(0);
    }

    std::size_t available() const noexcept { return static_cast<std::size_t>(m_write.load(std::memory_order_acquire) - m_read.load(std::memory_order_acquire)); }
    std::size_t space() const noexcept { return m_capacity - available(); }
    std::size_t capacity() const noexcept { return m_capacity; }

    std::size_t push(const float* frames, std::size_t count) noexcept {
        const std::uint64_t w = m_write.load(std::memory_order_relaxed);
        const std::size_t n = std::min(count, space());
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t at = static_cast<std::size_t>((w + i) % m_capacity) * m_channels;
            std::memcpy(&m_data[at], frames + i * m_channels, m_channels * sizeof(float));
        }
        m_write.store(w + n, std::memory_order_release);
        return n;
    }

    std::size_t pop(float* frames, std::size_t count) noexcept {
        const std::uint64_t r = m_read.load(std::memory_order_relaxed);
        const std::size_t n = std::min(count, available());
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t at = static_cast<std::size_t>((r + i) % m_capacity) * m_channels;
            std::memcpy(frames + i * m_channels, &m_data[at], m_channels * sizeof(float));
        }
        m_read.store(r + n, std::memory_order_release);
        return n;
    }

    void skip(std::size_t count) noexcept {
        const std::size_t n = std::min(count, available());
        m_read.fetch_add(n, std::memory_order_acq_rel);
    }

private:
    std::vector<float> m_data;
    std::size_t m_capacity = 0;
    std::size_t m_channels = 1;
    std::atomic<std::uint64_t> m_read{ 0 };
    std::atomic<std::uint64_t> m_write{ 0 };
};

inline void store_max(std::atomic<float>& a, float v) {
    float cur = a.load(std::memory_order_relaxed);
    while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
}

class duplex {
public:
    duplex() : m_state(std::make_shared<state>()) {}

    std::string start(const std::string& input, const std::string& output, long long rate, long long channels, long long latency_ms, bool loopback) {
        state& s = *m_state;
        if (s.out.running() || s.in.running()) return "the live chain is already running";
        s.out.close();
        s.in.close();
        std::string error;
        s.hold.hold();
        const fa::StreamConfig oc = make_config(fa::DeviceKind::output, output, rate, channels, latency_ms, "walnut live output");
        state* st = &s;
        if (!s.out.open_playback(oc, [st](float* out, std::size_t frames) { st->render(out, frames); }, &error)) {
            s.hold.release();
            return error.empty() ? std::string("can't open the output device") : error;
        }
        s.rate = s.out.sample_rate();
        s.channels = s.out.channels();
        const fa::StreamConfig ic = make_config(loopback ? fa::DeviceKind::loopback : fa::DeviceKind::input, input, s.rate, s.channels, latency_ms, "walnut live input");
        if (!s.in.open_capture(ic, [st](const float* in, std::size_t frames) { st->capture(in, frames); }, &error)) {
            s.out.close();
            s.hold.release();
            return error.empty() ? std::string("can't open the input device") : error;
        }
        s.in_rate = s.in.sample_rate();
        s.in_channels = s.in.channels();
        s.resample = s.in_rate != s.rate;
        if (s.resample) s.resampler.configure(s.channels, s.in_rate, s.rate, fa::ResampleQuality::medium);
        s.remixed.assign(16384 * std::max<std::size_t>(s.channels, 1), 0.0f);
        s.resampled.clear();
        s.resampled.reserve(32768 * std::max<std::size_t>(s.channels, 1));
        const std::size_t period = std::max<std::size_t>(64, static_cast<std::size_t>(s.rate) * static_cast<std::size_t>(std::max<long long>(latency_ms, 1)) / 1000);
        s.target = std::max<std::size_t>(period, std::max<std::size_t>(s.out.latency_frames(), s.in.latency_frames()));
        s.limit = s.target * 3 + s.rate / 50;
        s.queue.configure(std::max<std::size_t>(static_cast<std::size_t>(s.rate) * 2, s.limit * 2), s.channels);
        s.primed.store(false);
        s.underruns.store(0);
        s.overruns.store(0);
        s.dropped.store(0);
        s.chain->prepare(s.rate, s.channels);
        s.chain->reset();
        if (!s.out.start(&error) || !s.in.start(&error)) {
            s.in.close();
            s.out.close();
            s.hold.release();
            return error.empty() ? std::string("can't start the devices") : error;
        }
        return std::string();
    }

    void stop() {
        m_state->in.close();
        m_state->out.close();
        m_state->hold.release();
    }

    bool running() const { return m_state->out.running() && m_state->in.running(); }
    bool failed() const { return m_state->out.failed() || m_state->in.failed(); }

    std::string error() const {
        std::string e = m_state->in.error();
        if (e.empty()) e = m_state->out.error();
        return e;
    }

    chain effects() const { return chain(m_state->chain); }

    void set_effects(const chain& c) {
        if (!c.m || c.m == m_state->chain) return;
        std::vector<std::shared_ptr<fa::Effect>> list = c.m->effects();
        m_state->chain->assign(std::move(list));
    }

    void set_gain(const F& g) { m_state->gain.store(f(g)); }
    F gain() const { return w(m_state->gain.load()); }
    void set_bypass(bool on) { m_state->bypass.store(on); }
    bool bypass() const { return m_state->bypass.load(); }
    void set_muted(bool on) { m_state->muted.store(on); }
    bool muted() const { return m_state->muted.load(); }

    long long sample_rate() const { return static_cast<long long>(m_state->rate); }
    long long channels() const { return static_cast<long long>(m_state->channels); }
    std::string input_device() const { return m_state->in.device_name(); }
    std::string output_device() const { return m_state->out.device_name(); }
    long long underruns() const { return static_cast<long long>(m_state->underruns.load()); }
    long long overruns() const { return static_cast<long long>(m_state->overruns.load()); }
    long long dropped_frames() const { return static_cast<long long>(m_state->dropped.load()); }
    long long frames_processed() const { return static_cast<long long>(m_state->out.frames_processed()); }

    F input_peak() {
        return w(m_state->in_peak.exchange(0.0f));
    }

    F output_peak() {
        return w(m_state->out_peak.exchange(0.0f));
    }

    F latency_ms() const {
        const state& s = *m_state;
        if (!s.rate) return w(0.0);
        const double frames = static_cast<double>(s.out.latency_frames()) + static_cast<double>(s.in.latency_frames()) + static_cast<double>(s.queue.available()) + static_cast<double>(s.chain->latency_frames());
        return w(frames * 1000.0 / static_cast<double>(s.rate));
    }

private:
    struct state {
        gc_hold hold;
        std::shared_ptr<fa::EffectChain> chain = std::make_shared<fa::EffectChain>();
        ring queue;
        fa::Resampler resampler;
        std::vector<float> remixed;
        std::vector<float> resampled;
        std::uint32_t rate = 0;
        std::size_t channels = 0;
        std::uint32_t in_rate = 0;
        std::size_t in_channels = 0;
        bool resample = false;
        std::size_t target = 0;
        std::size_t limit = 0;
        std::atomic<bool> primed{ false };
        std::atomic<float> gain{ 1.0f };
        std::atomic<bool> bypass{ false };
        std::atomic<bool> muted{ false };
        std::atomic<std::uint64_t> underruns{ 0 };
        std::atomic<std::uint64_t> overruns{ 0 };
        std::atomic<std::uint64_t> dropped{ 0 };
        std::atomic<float> in_peak{ 0.0f };
        std::atomic<float> out_peak{ 0.0f };
        fa::Stream out;
        fa::Stream in;

        ~state() {
            in.close();
            out.close();
        }

        void capture(const float* input, std::size_t frames) noexcept {
            const std::size_t c = channels ? channels : 1;
            float peak = 0.0f;
            for (std::size_t i = 0; i < frames * in_channels; ++i) peak = std::max(peak, std::fabs(input[i]));
            store_max(in_peak, peak);
            std::size_t done = 0;
            while (done < frames) {
                const std::size_t n = std::min(frames - done, remixed.size() / c);
                fa::remix(input + done * in_channels, in_channels, remixed.data(), c, n);
                const float* data = remixed.data();
                std::size_t count = n;
                if (resample) {
                    resampled.clear();
                    count = resampler.process(remixed.data(), n, resampled);
                    data = resampled.data();
                }
                const std::size_t pushed = queue.push(data, count);
                if (pushed < count) overruns.fetch_add(count - pushed, std::memory_order_relaxed);
                done += n;
            }
        }

        void render(float* output, std::size_t frames) noexcept {
            const std::size_t c = channels ? channels : 1;
            const std::size_t have = queue.available();
            if (!primed.load(std::memory_order_relaxed)) {
                if (have >= target) {
                    primed.store(true, std::memory_order_relaxed);
                } else {
                    std::memset(output, 0, frames * c * sizeof(float));
                    return;
                }
            }
            if (have > limit + frames) {
                const std::size_t extra = have - target - frames;
                queue.skip(extra);
                dropped.fetch_add(extra, std::memory_order_relaxed);
            }
            const std::size_t got = queue.pop(output, frames);
            if (got < frames) {
                std::memset(output + got * c, 0, (frames - got) * c * sizeof(float));
                underruns.fetch_add(1, std::memory_order_relaxed);
                primed.store(false, std::memory_order_relaxed);
            }
            if (!bypass.load(std::memory_order_relaxed)) chain->process(output, frames);
            const float g = muted.load(std::memory_order_relaxed) ? 0.0f : gain.load(std::memory_order_relaxed);
            float peak = 0.0f;
            for (std::size_t i = 0; i < frames * c; ++i) {
                output[i] *= g;
                peak = std::max(peak, std::fabs(output[i]));
            }
            store_max(out_peak, peak);
        }
    };

    std::shared_ptr<state> m_state;
};

class sound {
public:
    sound() = default;
    explicit sound(fa::Sound s) : m(std::move(s)) {}
    explicit sound(const buffer& b) : m(fa::make_sound(b.b)) {}

    static sound load(const std::string& path) {
        buffer b = buffer::load(path);
        if (b.b.empty() && !last_error().empty()) return sound();
        return sound(fa::make_sound(std::move(b.b)));
    }

    static std::string load_error() { return last_error(); }

    bool valid() const { return m && !m->empty(); }
    long long channels() const { return m ? static_cast<long long>(m->channels()) : 0; }
    long long sample_rate() const { return m ? static_cast<long long>(m->sample_rate()) : 0; }
    long long frames() const { return m ? static_cast<long long>(m->frames()) : 0; }
    F duration() const { return w(m ? m->duration() : 0.0); }
    buffer to_buffer() const { return m ? buffer(*m) : buffer(); }

    fa::Sound m;
};

struct mixer_state {
    gc_hold hold;
    fa::Mixer mixer;
    std::shared_ptr<fa::EffectChain> chain = std::make_shared<fa::EffectChain>();
    fa::Stream out;

    mixer_state(std::uint32_t rate, std::size_t channels) : mixer(rate ? rate : 48000, channels ? channels : 2) {
        chain->prepare(mixer.sample_rate(), mixer.channels());
    }

    ~mixer_state() { out.close(); }

    void render(float* output, std::size_t frames) noexcept {
        mixer.render(output, frames);
        chain->process(output, frames);
    }
};

class voice {
public:
    voice() = default;
    voice(std::weak_ptr<mixer_state> m, std::uint64_t id) : m_mixer(std::move(m)), m_id(id) {}

    long long id() const { return static_cast<long long>(m_id); }
    bool valid() const { return m_id != 0; }
    bool is_playing() const { auto m = m_mixer.lock(); return m && m_id && m->mixer.is_playing(m_id); }
    bool is_paused() const { auto m = m_mixer.lock(); return m && m_id && m->mixer.is_paused(m_id); }
    void stop(const F& fade_out) { if (auto m = m_mixer.lock()) m->mixer.stop(m_id, std::max(0.0, d(fade_out))); }
    void pause() { if (auto m = m_mixer.lock()) m->mixer.set_paused(m_id, true); }
    void resume() { if (auto m = m_mixer.lock()) m->mixer.set_paused(m_id, false); }
    void set_gain(const F& g) { if (auto m = m_mixer.lock()) m->mixer.set_gain(m_id, f(g)); }
    void set_pan(const F& p) { if (auto m = m_mixer.lock()) m->mixer.set_pan(m_id, f(p)); }
    void set_speed(const F& s) { if (auto m = m_mixer.lock()) m->mixer.set_speed(m_id, f(s)); }
    void set_loop(bool on) { if (auto m = m_mixer.lock()) m->mixer.set_loop(m_id, on); }
    bool set_pitch(const F& semitones) { auto m = m_mixer.lock(); return m && m->mixer.set_pitch(m_id, f(semitones)); }
    bool set_preserve_pitch(bool on) { auto m = m_mixer.lock(); return m && m->mixer.set_preserve_pitch(m_id, on); }
    bool seek(const F& seconds) { auto m = m_mixer.lock(); return m && m->mixer.seek(m_id, std::max(0.0, d(seconds))); }
    F position() const { auto m = m_mixer.lock(); return w(m ? m->mixer.position(m_id) : 0.0); }
    F duration() const { auto m = m_mixer.lock(); return w(m ? m->mixer.duration(m_id) : 0.0); }

    bool wait(long long timeout_ms) const {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms < 0 ? 0 : timeout_ms);
        while (is_playing()) {
            auto m = m_mixer.lock();
            if (!m || !m->out.running()) return false;
            if (timeout_ms >= 0 && std::chrono::steady_clock::now() >= until) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return true;
    }

private:
    std::weak_ptr<mixer_state> m_mixer;
    std::uint64_t m_id = 0;
};

inline fa::PlayOptions play_options(const F& gain, const F& pan, const F& speed, bool loop, bool paused, const F& start, const F& fade_in, const F& pitch, bool preserve_pitch) {
    fa::PlayOptions o;
    o.gain = f(gain);
    o.pan = f(pan);
    o.speed = f(speed);
    o.loop = loop;
    o.paused = paused;
    o.start = std::max(0.0, d(start));
    o.fade_in = std::max(0.0, d(fade_in));
    o.pitch = f(pitch);
    o.preserve_pitch = preserve_pitch;
    o.stretchable = preserve_pitch || o.pitch != 0.0f;
    return o;
}

class mixer {
public:
    mixer() : m(std::make_shared<mixer_state>(48000, 2)) {}
    mixer(long long rate, long long channels) : m(std::make_shared<mixer_state>(rate_of(rate), std::max<std::size_t>(1, std::min<std::size_t>(count_of(channels), 8)))) {}

    voice play(const sound& s, const F& gain, const F& pan, const F& speed, bool loop, bool paused, const F& start, const F& fade_in, const F& pitch, bool preserve_pitch) {
        if (!s.m) return voice();
        return voice(m, m->mixer.play(s.m, play_options(gain, pan, speed, loop, paused, start, fade_in, pitch, preserve_pitch)));
    }

    voice play_buffer(const buffer& b, const F& gain, const F& pan, const F& speed, bool loop, bool paused, const F& start, const F& fade_in, const F& pitch, bool preserve_pitch) {
        return voice(m, m->mixer.play(b.b, play_options(gain, pan, speed, loop, paused, start, fade_in, pitch, preserve_pitch)));
    }

    voice play_file(const std::string& path, const F& gain, const F& pan, const F& speed, bool loop, bool paused, const F& start, const F& fade_in, const F& pitch, bool preserve_pitch) {
        std::string error;
        set_error(std::string());
        const std::uint64_t id = m->mixer.play_file(path, play_options(gain, pan, speed, loop, paused, start, fade_in, pitch, preserve_pitch), &error);
        if (!id) set_error(error.empty() ? std::string("can't play the file") : error);
        return voice(m, id);
    }

    static std::string play_error() { return last_error(); }

    void stop_all(const F& fade_out) { m->mixer.stop_all(std::max(0.0, d(fade_out))); }
    long long voice_count() const { return static_cast<long long>(m->mixer.voice_count()); }
    void set_master_gain(const F& g) { m->mixer.set_master_gain(f(g)); }
    F master_gain() const { return w(m->mixer.master_gain()); }
    F output_peak() const { return w(m->mixer.output_peak()); }
    long long sample_rate() const { return static_cast<long long>(m->mixer.sample_rate()); }
    long long channels() const { return static_cast<long long>(m->mixer.channels()); }
    chain effects() const { return chain(m->chain); }

    buffer render(long long frames) {
        if (m->out.running()) fail("Mixer::render(): the mixer is already playing through a device; call stop_output() first");
        fa::AudioBuffer out(m->mixer.channels(), m->mixer.sample_rate(), count_of(frames));
        const std::size_t block = 1024;
        for (std::size_t pos = 0; pos < out.frames(); pos += block) m->render(out.frame(pos), std::min(block, out.frames() - pos));
        return buffer(std::move(out));
    }

    std::string start(const std::string& device, long long latency_ms) {
        if (m->out.running()) return std::string();
        m->out.close();
        std::string error;
        fa::StreamConfig c = make_config(fa::DeviceKind::output, device, m->mixer.sample_rate(), static_cast<long long>(m->mixer.channels()), latency_ms, "walnut mixer");
        mixer_state* st = m.get();
        m->hold.hold();
        if (!m->out.open_playback(c, [st](float* out, std::size_t frames) { st->render(out, frames); }, &error) || !m->out.start(&error)) {
            m->out.close();
            m->hold.release();
            return error.empty() ? std::string("can't open the output device") : error;
        }
        return std::string();
    }

    void stop_output() {
        m->out.close();
        m->hold.release();
    }

    bool output_running() const { return m->out.running(); }
    bool output_failed() const { return m->out.failed(); }
    std::string output_error() const { return m->out.error(); }
    std::string device_name() const { return m->out.device_name(); }

    bool wait_idle(long long timeout_ms) const {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms < 0 ? 0 : timeout_ms);
        while (m->mixer.voice_count() > 0) {
            if (!m->out.running()) return false;
            if (timeout_ms >= 0 && std::chrono::steady_clock::now() >= until) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return true;
    }

    std::shared_ptr<mixer_state> shared_state() const { return m; }

private:
    std::shared_ptr<mixer_state> m;
};

struct timeline_state {
    fa::Timeline t;
    timeline_state(std::uint32_t rate, std::size_t channels) : t(rate ? rate : 48000, channels ? channels : 2) {}
};

class track {
public:
    track() = default;
    track(std::shared_ptr<timeline_state> t, std::size_t index) : m_t(std::move(t)), m_index(index) {}

    std::string name() const { return get().name; }
    void set_name(const std::string& n) { get().name = n; }
    long long index() const { return static_cast<long long>(m_index); }
    void set_volume_db(const F& db) { get().volume_db = f(db); }
    F volume_db() const { return w(get().volume_db); }
    void set_pan(const F& p) { get().pan = std::max(-1.0f, std::min(1.0f, f(p))); }
    F pan() const { return w(get().pan); }
    void set_mute(bool on) { get().mute = on; }
    bool mute() const { return get().mute; }
    void set_solo(bool on) { get().solo = on; }
    bool solo() const { return get().solo; }
    F length() const { return w(get().length()); }

    long long add_clip(const sound& s, const F& position, const std::string& name) {
        if (!s.m) fail("Track::add_clip(): the sound is empty");
        return static_cast<long long>(get().add_clip(fa::make_clip(s.m, std::max(0.0, d(position)), name)));
    }

    long long add_buffer(const buffer& b, const F& position, const std::string& name) { return static_cast<long long>(get().add_clip(fa::make_clip(b.b, std::max(0.0, d(position)), name))); }
    long long clip_count() const { return static_cast<long long>(get().clips.size()); }
    bool remove_clip(long long i) { return i >= 0 && get().remove_clip(static_cast<std::size_t>(i)); }
    bool split_clip(long long i, const F& time) { return i >= 0 && get().split_clip(static_cast<std::size_t>(i), d(time)); }
    F clip_position(long long i) const { return w(clip(i).position); }
    F clip_length(long long i) const { return w(clip(i).length); }
    F clip_end(long long i) const { return w(clip(i).end()); }
    std::string clip_name(long long i) const { return clip(i).name; }
    void move_clip(long long i, const F& time) { clip(i).move_to(std::max(0.0, d(time))); }
    void set_clip_gain_db(long long i, const F& db) { clip(i).gain_db = f(db); }
    void set_clip_muted(long long i, bool on) { clip(i).muted = on; }
    void trim_clip_start(long long i, const F& time) { clip(i).trim_start(d(time)); }
    void trim_clip_end(long long i, const F& time) { clip(i).trim_end(d(time)); }

    void set_clip_fades(long long i, const F& in, const F& out, long long curve) {
        fa::Clip& c = clip(i);
        c.fade_in = std::max(0.0, d(in));
        c.fade_out = std::max(0.0, d(out));
        c.fade_in_shape = shape_of(curve);
        c.fade_out_shape = shape_of(curve);
    }

    chain effects() {
        fa::Track& t = get();
        if (!t.effects) t.effects = std::make_shared<fa::EffectChain>();
        return chain(t.effects);
    }

    void automate(long long target, long long effect_index, const std::string& parameter, const F& time, const F& value, long long curve) {
        fa::AutomationTarget at = target == 1 ? fa::AutomationTarget::pan : target == 2 ? fa::AutomationTarget::mute : target == 3 ? fa::AutomationTarget::effect_parameter : fa::AutomationTarget::volume;
        get().lane(at, count_of(effect_index), parameter).add(std::max(0.0, d(time)), f(value), automation_curve_of(curve));
    }

    void clear_automation() { get().lanes.clear(); }

private:
    fa::Track& get() const {
        if (!m_t || m_index >= m_t->t.tracks.size()) fail("this Track was removed from its Timeline");
        return m_t->t.tracks[m_index];
    }

    fa::Clip& clip(long long i) const {
        fa::Track& t = get();
        if (i < 0 || static_cast<std::size_t>(i) >= t.clips.size()) fail("clip " + std::to_string(i) + " is out of range for a track with " + std::to_string(t.clips.size()) + " clips");
        return t.clips[static_cast<std::size_t>(i)];
    }

    std::shared_ptr<timeline_state> m_t;
    std::size_t m_index = 0;
};

class timeline {
public:
    timeline() : m(std::make_shared<timeline_state>(48000, 2)) {}
    timeline(long long rate, long long channels) : m(std::make_shared<timeline_state>(rate_of(rate), std::max<std::size_t>(1, std::min<std::size_t>(count_of(channels), 8)))) {}

    long long sample_rate() const { return static_cast<long long>(m->t.sample_rate()); }
    long long channels() const { return static_cast<long long>(m->t.channels()); }

    track add_track(const std::string& name) {
        m->t.add_track(name);
        return track(m, m->t.tracks.size() - 1);
    }

    track track_at(long long i) const {
        if (i < 0 || static_cast<std::size_t>(i) >= m->t.tracks.size()) fail("track " + std::to_string(i) + " is out of range for a timeline with " + std::to_string(m->t.tracks.size()) + " tracks");
        return track(m, static_cast<std::size_t>(i));
    }

    long long track_count() const { return static_cast<long long>(m->t.tracks.size()); }
    bool remove_track(long long i) { return i >= 0 && m->t.remove_track(static_cast<std::size_t>(i)); }
    F length() const { return w(m->t.length()); }
    void set_tempo(const F& bpm) { m->t.tempo = std::max(1.0, d(bpm)); }
    F tempo() const { return w(m->t.tempo); }
    F beats_to_seconds(const F& beats) const { return w(m->t.beats_to_seconds(d(beats))); }
    F bars_to_seconds(const F& bars) const { return w(m->t.bars_to_seconds(d(bars))); }
    F snap(const F& seconds, const F& division) const { return w(m->t.snap(d(seconds), d(division))); }
    long long add_marker(const F& time, const std::string& name) { return static_cast<long long>(m->t.add_marker(std::max(0.0, d(time)), name)); }
    long long marker_count() const { return static_cast<long long>(m->t.markers.size()); }
    F marker_time(long long i) const { return w(i >= 0 && static_cast<std::size_t>(i) < m->t.markers.size() ? m->t.markers[static_cast<std::size_t>(i)].time : 0.0); }
    std::string marker_name(long long i) const { return i >= 0 && static_cast<std::size_t>(i) < m->t.markers.size() ? m->t.markers[static_cast<std::size_t>(i)].name : std::string(); }
    void set_master_volume_db(const F& db) { m->t.master_volume_db = f(db); }
    F master_volume_db() const { return w(m->t.master_volume_db); }

    void set_loop(const F& start, const F& end, bool on) {
        m->t.loop_start = std::max(0.0, d(start));
        m->t.loop_end = std::max(m->t.loop_start, d(end));
        m->t.loop_enabled = on;
    }

    chain master_effects() {
        if (!m->t.master_effects) m->t.master_effects = std::make_shared<fa::EffectChain>();
        return chain(m->t.master_effects);
    }

    void automate_master_volume(const F& time, const F& db, long long curve) {
        m->t.master_lane(fa::AutomationTarget::volume).add(std::max(0.0, d(time)), f(db), automation_curve_of(curve));
    }

    buffer render(bool include_tail) const {
        fa::RenderOptions o;
        o.include_tail = include_tail;
        return buffer(fa::render(m->t, o));
    }

    buffer render_range(const F& start, const F& end, bool include_tail) const {
        fa::RenderOptions o;
        o.include_tail = include_tail;
        return buffer(fa::render(m->t, std::max(0.0, d(start)), std::max(0.0, d(end)), o));
    }

    const fa::Timeline& native() const { return m->t; }

private:
    std::shared_ptr<timeline_state> m;
};

struct player_state {
    gc_hold hold;
    fa::TimelinePlayer player;
    fa::Stream out;
    player_state(std::uint32_t rate, std::size_t channels) : player(rate ? rate : 48000, channels ? channels : 2) {}
    ~player_state() { out.close(); }
};

class timeline_player {
public:
    timeline_player() : m(std::make_shared<player_state>(48000, 2)) {}
    timeline_player(long long rate, long long channels) : m(std::make_shared<player_state>(rate_of(rate), std::max<std::size_t>(1, std::min<std::size_t>(count_of(channels), 8)))) {}

    void update(const timeline& t) { m->player.update(t.native()); }
    void play() { m->player.play(); }
    void pause() { m->player.pause(); }
    void stop() { m->player.stop(); }
    void seek(const F& seconds) { m->player.seek(std::max(0.0, d(seconds))); }
    void set_loop(const F& start, const F& end, bool on) { m->player.set_loop(std::max(0.0, d(start)), std::max(0.0, d(end)), on); }
    bool is_playing() const { return m->player.is_playing(); }
    F position() const { return w(m->player.position()); }
    F output_peak() const { return w(m->player.output_peak()); }
    long long sample_rate() const { return static_cast<long long>(m->player.sample_rate()); }
    long long channels() const { return static_cast<long long>(m->player.channels()); }

    buffer render(long long frames) {
        if (m->out.running()) fail("TimelinePlayer::render(): the player is already playing through a device; call stop_output() first");
        fa::AudioBuffer out(m->player.channels(), m->player.sample_rate(), count_of(frames));
        const std::size_t block = 1024;
        for (std::size_t pos = 0; pos < out.frames(); pos += block) m->player.render(out.frame(pos), std::min(block, out.frames() - pos));
        return buffer(std::move(out));
    }

    std::string start(const std::string& device, long long latency_ms) {
        if (m->out.running()) return std::string();
        m->out.close();
        std::string error;
        fa::StreamConfig c = make_config(fa::DeviceKind::output, device, m->player.sample_rate(), static_cast<long long>(m->player.channels()), latency_ms, "walnut timeline");
        player_state* st = m.get();
        m->hold.hold();
        if (!m->out.open_playback(c, [st](float* out, std::size_t frames) { st->player.render(out, frames); }, &error) || !m->out.start(&error)) {
            m->out.close();
            m->hold.release();
            return error.empty() ? std::string("can't open the output device") : error;
        }
        return std::string();
    }

    void stop_output() {
        m->out.close();
        m->hold.release();
    }

    bool output_running() const { return m->out.running(); }

private:
    std::shared_ptr<player_state> m;
};

inline std::vector<std::string> voice_preset_names() {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < fa::voice_preset_count(); ++i) out.push_back(fa::voice_preset_name(static_cast<fa::VoicePreset>(i)));
    return out;
}

} // namespace audio
} // namespace walnut_rt

#endif // WALNUT_AUDIO_HPP
