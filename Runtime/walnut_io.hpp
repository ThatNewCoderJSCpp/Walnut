#ifndef WALNUT_IO_HPP
#define WALNUT_IO_HPP

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "walnut_special_names.hpp"

namespace walnut_rt {
namespace io_native {

inline std::size_t utf8_length(unsigned char lead) {
    if (lead < 0x80) return 1;
    if (lead >= 0xF0) return 4;
    if (lead >= 0xE0) return 3;
    if (lead >= 0xC0) return 2;
    return 1;
}

inline char32_t utf8_decode(const std::string& s, std::size_t at, std::size_t& used) {
    const unsigned char lead = static_cast<unsigned char>(s[at]);
    used = utf8_length(lead);
    if (at + used > s.size()) used = s.size() - at;
    if (used == 1) return lead;
    char32_t cp = lead & (used == 2 ? 0x1F : used == 3 ? 0x0F : 0x07);
    for (std::size_t k = 1; k < used; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[at + k]) & 0x3F);
    return cp;
}

class device {
public:
    virtual ~device() = default;

    virtual bool write(const std::string& bytes) = 0;
    virtual bool flush() { return true; }
    virtual std::int64_t seek(std::int64_t, int, bool, bool) { return -1; }
    virtual std::int64_t available() { return 0; }

    bool peek_ok() {
        if (m_pos < m_in.size()) return true;
        return refill();
    }

    char32_t peek() {
        if (!peek_ok()) return 0;
        ensure_whole_char();
        std::size_t used = 0;
        return utf8_decode(m_in, m_pos, used);
    }

    void bump() {
        if (!peek_ok()) return;
        ensure_whole_char();
        std::size_t used = 0;
        utf8_decode(m_in, m_pos, used);
        m_last = m_pos;
        m_pos += used;
        compact();
    }

    bool unget() {
        if (m_last == npos || m_last >= m_pos) return false;
        m_pos = m_last;
        m_last = npos;
        return true;
    }

    std::string read_bytes(std::int64_t count) {
        std::string out;
        while (count > 0 && peek_ok()) {
            const std::size_t take = std::min<std::size_t>(static_cast<std::size_t>(count), m_in.size() - m_pos);
            out.append(m_in, m_pos, take);
            m_pos += take;
            count -= static_cast<std::int64_t>(take);
        }
        m_last = npos;
        compact();
        return out;
    }

protected:
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    virtual bool refill() = 0;

    void ensure_whole_char() {
        const std::size_t need = utf8_length(static_cast<unsigned char>(m_in[m_pos]));
        while (m_in.size() - m_pos < need && refill()) {}
    }

    void compact() {
        if (m_pos > 4096 && m_pos * 2 > m_in.size()) {
            m_in.erase(0, m_pos - 4);
            m_last = (m_last == npos || m_last < m_pos - 4) ? npos : m_last - (m_pos - 4);
            m_pos = 4;
        }
    }

    std::string m_in;
    std::size_t m_pos  = 0;
    std::size_t m_last = npos;
};

class file_device : public device {
public:
    file_device(std::FILE* f, bool owned) : m_file(f), m_owned(owned) {}
    ~file_device() override { if (m_owned && m_file) std::fclose(m_file); }

    bool write(const std::string& bytes) override {
        if (!m_file) return false;
        if (m_reading) { std::fseek(m_file, 0, SEEK_CUR); m_reading = false; }
        return std::fwrite(bytes.data(), 1, bytes.size(), m_file) == bytes.size();
    }

    bool flush() override { return m_file && std::fflush(m_file) == 0; }

    std::int64_t seek(std::int64_t off, int dir, bool, bool) override {
        if (!m_file) return -1;
        const int whence = dir == 0 ? SEEK_SET : dir == 1 ? SEEK_CUR : SEEK_END;
        if (dir == 1) off -= static_cast<std::int64_t>(m_in.size() - m_pos);
        m_in.clear(); m_pos = 0; m_last = npos;
        if (std::fseek(m_file, static_cast<long>(off), whence) != 0) return -1;
        return static_cast<std::int64_t>(std::ftell(m_file));
    }

    std::int64_t available() override { return static_cast<std::int64_t>(m_in.size() - m_pos); }

protected:
    bool refill() override {
        if (!m_file) return false;
        char chunk[4096];
        const bool interactive = m_file == stdin;
        std::size_t got = 0;

        if (interactive) {
            if (!std::fgets(chunk, sizeof chunk, m_file)) return false;
            got = std::char_traits<char>::length(chunk);
        } else {
            got = std::fread(chunk, 1, sizeof chunk, m_file);
        }

        if (got == 0) return false;
        m_reading = true;
        m_in.append(chunk, got);
        return true;
    }

private:
    std::FILE* m_file;
    bool       m_owned;
    bool       m_reading = false;
};

class string_device : public device {
public:
    string_device(std::string initial, bool append, bool at_end) : m_append(append) {
        m_in = std::move(initial);
        m_write = at_end ? m_in.size() : 0;
    }

    bool write(const std::string& bytes) override {
        if (m_append) m_write = m_in.size();
        if (m_write >= m_in.size()) { m_in.append(bytes); m_write = m_in.size(); return true; }
        const std::size_t overlap = std::min(bytes.size(), m_in.size() - m_write);
        m_in.replace(m_write, overlap, bytes, 0, overlap);
        if (overlap < bytes.size()) m_in.append(bytes, overlap, std::string::npos);
        m_write += bytes.size();
        return true;
    }

    std::int64_t seek(std::int64_t off, int dir, bool in, bool out) override {
        const std::int64_t size = static_cast<std::int64_t>(m_in.size());
        auto target = [&](std::int64_t cur) { return dir == 0 ? off : dir == 1 ? cur + off : size + off; };
        std::int64_t result = -1;

        if (in) {
            const std::int64_t t = target(static_cast<std::int64_t>(m_pos));
            if (t < 0 || t > size) return -1;
            m_pos = static_cast<std::size_t>(t); m_last = npos; result = t;
        }

        if (out) {
            const std::int64_t t = target(static_cast<std::int64_t>(m_write));
            if (t < 0 || t > size) return -1;
            m_write = static_cast<std::size_t>(t); result = t;
        }

        return result;
    }

    std::int64_t available() override { return static_cast<std::int64_t>(m_in.size() - m_pos); }

    const std::string& str() const { return m_in; }

    void set_str(const std::string& s, bool at_end) {
        m_in = s; m_pos = 0; m_last = npos;
        m_write = at_end ? m_in.size() : 0;
    }

protected:
    bool refill() override { return false; }

private:
    std::size_t m_write = 0;
    bool        m_append = false;
};

struct registry {
    std::mutex                           lock;
    std::vector<std::unique_ptr<device>> items;
    std::vector<std::int64_t>            free_ids;

    registry() {
        items.emplace_back(new file_device(stdin, false));
        items.emplace_back(new file_device(stdout, false));
        items.emplace_back(new file_device(stderr, false));
    }

    std::int64_t add(device* d) {
        std::lock_guard<std::mutex> g(lock);
        if (!free_ids.empty()) { const std::int64_t id = free_ids.back(); free_ids.pop_back(); items[static_cast<std::size_t>(id)].reset(d); return id; }
        items.emplace_back(d);
        return static_cast<std::int64_t>(items.size() - 1);
    }

    device* get(std::int64_t id) {
        std::lock_guard<std::mutex> g(lock);
        if (id < 0 || static_cast<std::size_t>(id) >= items.size()) return nullptr;
        return items[static_cast<std::size_t>(id)].get();
    }

    void remove(std::int64_t id) {
        std::lock_guard<std::mutex> g(lock);
        if (id < 3 || static_cast<std::size_t>(id) >= items.size() || !items[static_cast<std::size_t>(id)]) return;
        items[static_cast<std::size_t>(id)].reset();
        free_ids.push_back(id);
    }
};

inline registry& devices() {
    static registry* r = new registry();
    return *r;
}

inline std::mutex& sync_mutex() {
    static std::mutex* m = new std::mutex();
    return *m;
}

inline std::int64_t file_open(const std::string& path, std::int64_t mode) {
    const bool app = mode & 1, bin = mode & 2, in = mode & 4, out = mode & 8, trunc = mode & 16, noreplace = mode & 64;
    std::string m;
    if (noreplace && out) { if (std::FILE* probe = std::fopen(path.c_str(), "rb")) { std::fclose(probe); return -1; } }
    if (app)                    m = in ? "a+" : "a";
    else if (in && out && trunc) m = "w+";
    else if (in && out)          m = "r+";
    else if (out)                m = "w";
    else                         m = "r";
    if (bin) m += 'b';
    std::FILE* f = std::fopen(path.c_str(), m.c_str());
    if (!f) return -1;
    const std::int64_t id = devices().add(new file_device(f, true));
    if (mode & 32) devices().get(id)->seek(0, 2, true, true);
    return id;
}

inline bool device_close(std::int64_t id) {
    device* d = devices().get(id);
    if (!d) return false;
    d->flush();
    devices().remove(id);
    return true;
}

inline std::int64_t string_open(const std::string& initial, std::int64_t mode) {
    return devices().add(new string_device(initial, (mode & 1) != 0, (mode & 33) != 0));
}

inline std::string string_get(std::int64_t id) {
    if (auto* d = dynamic_cast<string_device*>(devices().get(id))) return d->str();
    return std::string();
}

inline void string_set(std::int64_t id, const std::string& s, std::int64_t mode) {
    if (auto* d = dynamic_cast<string_device*>(devices().get(id))) d->set_str(s, (mode & 33) != 0);
}

inline bool device_write(std::int64_t id, const std::string& bytes) {
    device* d = devices().get(id);
    return d && d->write(bytes);
}

inline bool device_flush(std::int64_t id) {
    device* d = devices().get(id);
    return d && d->flush();
}

inline bool device_peek_ok(std::int64_t id) {
    device* d = devices().get(id);
    return d && d->peek_ok();
}

inline char32_t device_peek(std::int64_t id) {
    device* d = devices().get(id);
    return d ? d->peek() : char32_t(0);
}

inline void device_bump(std::int64_t id) {
    if (device* d = devices().get(id)) d->bump();
}

inline bool device_unget(std::int64_t id) {
    device* d = devices().get(id);
    return d && d->unget();
}

inline std::string device_read(std::int64_t id, std::int64_t count) {
    device* d = devices().get(id);
    return d ? d->read_bytes(count) : std::string();
}

inline std::int64_t device_seek(std::int64_t id, std::int64_t off, std::int64_t dir, std::int64_t which) {
    device* d = devices().get(id);
    return d ? d->seek(off, static_cast<int>(dir), (which & 4) != 0, (which & 8) != 0) : -1;
}

inline std::int64_t device_available(std::int64_t id) {
    device* d = devices().get(id);
    return d ? d->available() : 0;
}

inline void sync_acquire() { sync_mutex().lock(); }
inline void sync_release() { sync_mutex().unlock(); }

inline std::string with_base_prefix(std::string digits, std::int64_t base, bool upper) {
    if (base == 16) return (upper ? "0X" : "0x") + digits;
    if (base == 8)  return digits == "0" ? digits : "0" + digits;
    if (base == 2)  return (upper ? "0B" : "0b") + digits;
    return digits;
}

inline std::string upper_ascii(std::string s) {
    for (char& c : s) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    return s;
}

inline std::string format_int(walnut_rt::i2048 v, std::int64_t base, bool upper, bool showbase, bool showpos) {
    if (v.is_special()) return v.to_string();
    const bool neg = v.is_negative();
    std::string digits = v.raw().abs().to_string(base);
    if (upper) digits = upper_ascii(digits);
    if (showbase) digits = with_base_prefix(digits, base, upper);
    if (neg) return "-" + digits;
    return showpos ? "+" + digits : digits;
}

inline std::string format_uint(walnut_rt::u2048 v, std::int64_t base, bool upper, bool showbase, bool showpos) {
    if (v.is_special()) return v.to_string();
    std::string digits = v.raw().to_string(base);
    if (upper) digits = upper_ascii(digits);
    if (showbase) digits = with_base_prefix(digits, base, upper);
    return showpos ? "+" + digits : digits;
}

template <typename F>
inline std::string general_text(const F& f, std::int64_t precision) {
    return walnut_rt::float_display(f, precision);
}

template <typename F>
inline std::string float_text(const F& f, std::int64_t precision, std::int64_t style) {
    if (style == 2) return f.to_scientific_string(precision + 1);
    if (style != 1) return general_text(f, precision);
    if (!f.is_finite()) return f.to_string();
    const std::int64_t decimals = precision < 0 ? 6 : precision;
    if (f.is_zero()) return (f.is_negative() ? "-0" : "0") + (decimals > 0 ? "." + std::string(static_cast<std::size_t>(decimals), '0') : std::string());
    std::int64_t exp10 = 0;

    for (int attempt = 0; attempt < 2; ++attempt) {
        const std::string probe = f.to_scientific_string(1);
        const std::size_t e = probe.find('e');
        exp10 = std::stoll(probe.substr(e + 1));
        const std::int64_t sig = exp10 + 1 + decimals;
        if (sig <= 0) return (f.is_negative() ? "-0" : "0") + (decimals > 0 ? "." + std::string(static_cast<std::size_t>(decimals), '0') : std::string());
        const std::string sci = f.to_scientific_string(sig);
        const std::size_t ep = sci.find('e');
        const std::int64_t got = std::stoll(sci.substr(ep + 1));
        std::string mant = sci.substr(0, ep);
        const bool neg = !mant.empty() && mant[0] == '-';
        if (neg) mant.erase(0, 1);
        std::string digits;
        for (char c : mant) if (c != '.') digits += c;
        if (got != exp10 && attempt == 0) { exp10 = got; continue; }
        while (static_cast<std::int64_t>(digits.size()) < got + 1 + decimals) digits += '0';
        std::string whole, frac;

        if (got >= 0) {
            whole = digits.substr(0, static_cast<std::size_t>(got + 1));
            frac  = digits.substr(static_cast<std::size_t>(got + 1));
        } else {
            whole = "0";
            frac  = std::string(static_cast<std::size_t>(-got - 1), '0') + digits;
        }

        frac = frac.substr(0, static_cast<std::size_t>(decimals));
        while (static_cast<std::int64_t>(frac.size()) < decimals) frac += '0';
        return (neg ? "-" : "") + whole + (decimals > 0 ? "." + frac : std::string());
    }

    return f.to_string();
}

inline std::string widen_exponent(const std::string& s) {
    const std::size_t e = s.find_first_of("eE");
    if (e == std::string::npos || e + 1 >= s.size()) return s;
    std::size_t d = e + 1;
    if (s[d] == '+' || s[d] == '-') ++d;
    if (s.size() - d >= 2) return s;
    std::string sign = d > e + 1 ? s.substr(e + 1, 1) : std::string("+");
    return s.substr(0, e + 1) + sign + "0" + s.substr(d);
}

inline std::string format_float(walnut_rt::f2048 v, std::int64_t bits, std::int64_t precision, std::int64_t style, bool upper, bool showpos, bool showpoint) {
    std::string out;

    switch (bits) {
        case 32:   out = float_text(walnut_rt::f32(v), precision, style);   break;
        case 64:   out = float_text(walnut_rt::f64(v), precision, style);   break;
        case 128:  out = float_text(walnut_rt::f128(v), precision, style);  break;
        case 256:  out = float_text(walnut_rt::f256(v), precision, style);  break;
        case 512:  out = float_text(walnut_rt::f512(v), precision, style);  break;
        case 1024: out = float_text(walnut_rt::f1024(v), precision, style); break;
        default:   out = float_text(v, precision, style);                   break;
    }

    if (v.is_finite()) out = widen_exponent(out);
    if (showpoint && v.is_finite() && out.find_first_of(".e") == std::string::npos) out += ".";
    if (upper) out = upper_ascii(out);
    if (showpos && v.is_finite() && !v.is_negative()) out = "+" + out;
    return out;
}

inline bool special_word(const std::string& s, walnut_rt::special_kind& k) {
    std::string w = s;
    bool neg = false;
    if (!w.empty() && (w[0] == '-' || w[0] == '+')) { neg = w[0] == '-'; w.erase(0, 1); }
    for (char& c : w) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    if (w == "inf" || w == "infinity" || w == "\xE2\x88\x9E") { k = neg ? walnut_rt::special_kind::neg_inf : walnut_rt::special_kind::pos_inf; return true; }
    if (neg) return false;
    if (w == "nan")           { k = walnut_rt::special_kind::nan;           return true; }
    if (w == "undefined")     { k = walnut_rt::special_kind::undefined;     return true; }
    if (w == "indeterminate") { k = walnut_rt::special_kind::indeterminate; return true; }
    return false;
}

inline bool digits_ok(const std::string& s, std::int64_t base, bool allow_sign) {
    std::size_t i = 0;
    if (allow_sign && !s.empty() && (s[0] == '-' || s[0] == '+')) i = 1;
    if (base == 16 && s.size() > i + 2 && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) i += 2;
    if (i >= s.size()) return false;

    for (; i < s.size(); ++i) {
        const char c = s[i];
        int d = -1;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
        else if (c == '_' || c == '\'') continue;
        if (d < 0 || d >= base) return false;
    }

    return true;
}

inline std::string strip_base_prefix(const std::string& s, std::int64_t& base) {
    std::size_t i = 0;
    std::string sign;
    if (!s.empty() && (s[0] == '-' || s[0] == '+')) { sign = s.substr(0, 1); i = 1; }

    if (s.size() > i + 2 && s[i] == '0') {
        const char p = s[i + 1];
        std::int64_t found = 0;
        if (p == 'x' || p == 'X') found = 16;
        else if (p == 'b' || p == 'B') found = 2;
        else if (p == 'o' || p == 'O') found = 8;
        if (found != 0 && (base == 0 || base == found)) { base = found; return sign + s.substr(i + 2); }
    }

    if (base == 0) base = 10;
    return s;
}

inline bool parse_int_ok(const std::string& s, std::int64_t base) {
    walnut_rt::special_kind k;
    if (special_word(s, k)) return true;
    if (base != 0 && (base < 2 || base > 36)) return false;
    const std::string body = strip_base_prefix(s, base);
    return digits_ok(body, base, true);
}

inline walnut_rt::i2048 parse_int(const std::string& s, std::int64_t base) {
    walnut_rt::special_kind k;
    if (special_word(s, k)) return walnut_rt::i2048::from_kind(k);
    if (!parse_int_ok(s, base)) return walnut_rt::i2048::undefined();
    const std::string body = strip_base_prefix(s, base);
    std::string clean;
    for (char c : body) if (c != '_' && c != '\'' && c != '+') clean += c;
    return walnut_rt::i2048(mp::int2048(clean, base));
}

inline bool parse_uint_ok(const std::string& s, std::int64_t base) {
    walnut_rt::special_kind k;
    if (special_word(s, k)) return k != walnut_rt::special_kind::neg_inf;
    if (base != 0 && (base < 2 || base > 36)) return false;
    const std::string body = strip_base_prefix(s, base);
    return digits_ok(body, base, false) || (body.size() > 1 && body[0] == '+' && digits_ok(body.substr(1), base, false));
}

inline walnut_rt::u2048 parse_uint(const std::string& s, std::int64_t base) {
    walnut_rt::special_kind k;
    if (special_word(s, k)) return walnut_rt::u2048::from_kind(k);
    if (!parse_uint_ok(s, base)) return walnut_rt::u2048::undefined();
    const std::string body = strip_base_prefix(s, base);
    std::string clean;
    for (char c : body) if (c != '_' && c != '\'' && c != '+') clean += c;
    return walnut_rt::u2048(mp::uint2048(clean, base));
}

inline bool hex_value(char c, unsigned& v) {
    if (c >= '0' && c <= '9') { v = static_cast<unsigned>(c - '0'); return true; }
    if (c >= 'a' && c <= 'f') { v = static_cast<unsigned>(c - 'a' + 10); return true; }
    if (c >= 'A' && c <= 'F') { v = static_cast<unsigned>(c - 'A' + 10); return true; }
    return false;
}

inline bool unescape_into(const std::string& s, std::string& out) {
    std::size_t i = 0;
    const std::size_t n = s.size();

    auto read_hex = [&](std::size_t count, char32_t& cp) {
        cp = 0;
        for (std::size_t k = 0; k < count; ++k) {
            unsigned v = 0;
            if (i >= n || !hex_value(s[i], v)) return false;
            cp = cp * 16 + v;
            ++i;
        }
        return true;
    };

    while (i < n) {
        const char c = s[i++];
        if (c != '\\') { out += c; continue; }
        if (i >= n) return false;
        const char e = s[i++];
        char32_t cp = 0;

        switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'a': out += '\a'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'v': out += '\v'; break;
            case 'e': out += '\x1B'; break;
            case '0': out += '\0'; break;
            case '\\': case '\'': case '"': case '?': case '`': case '$': out += e; break;
            case 'x':
                if (!read_hex(2, cp)) return false;
                walnut_rt::append_utf8(out, cp);
                break;
            case 'u':
                if (i < n && s[i] == '{') {
                    ++i;
                    std::size_t digits = 0;
                    unsigned v = 0;
                    while (i < n && hex_value(s[i], v)) { cp = cp * 16 + v; ++i; if (++digits > 6) return false; }
                    if (digits == 0 || i >= n || s[i] != '}') return false;
                    ++i;
                } else if (!read_hex(4, cp)) {
                    return false;
                }
                if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
                walnut_rt::append_utf8(out, cp);
                break;
            case 'U':
                if (!read_hex(8, cp) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
                walnut_rt::append_utf8(out, cp);
                break;
            case 's': {
                if (i >= n || s[i] != '{') return false;
                const std::size_t open = ++i;
                while (i < n && s[i] != '}') ++i;
                if (i == open || i >= n) return false;
                std::uint32_t code = 0;
                if (!walnut_rt::special_character(std::string_view(s).substr(open, i - open), code)) return false;
                ++i;
                walnut_rt::append_utf8(out, static_cast<char32_t>(code));
                break;
            }
            default: return false;
        }
    }

    return true;
}

inline std::string literal_body(const std::string& s, bool& quoted) {
    quoted = s.size() >= 2 && (s.front() == '"' || s.front() == '`' || s.front() == '\'') && s.back() == s.front();
    return quoted ? s.substr(1, s.size() - 2) : s;
}

inline bool unescape_ok(const std::string& s) {
    bool quoted = false;
    const std::string body = literal_body(s, quoted);
    if (!quoted) return true;
    std::string out;
    return unescape_into(body, out);
}

inline std::string unescape(const std::string& s) {
    bool quoted = false;
    const std::string body = literal_body(s, quoted);
    if (!quoted) return s;
    std::string out;
    return unescape_into(body, out) ? out : std::string();
}

inline std::string trim_spaces(const std::string& s) {
    const char* ws = " \t\n\r\f\v";
    const std::size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) return std::string();
    return s.substr(b, s.find_last_not_of(ws) - b + 1);
}

inline bool parse_float_ok(const std::string& s) {
    walnut_rt::special_kind k;
    if (special_word(s, k)) return true;
    std::size_t i = 0, digits = 0;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
    while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '_' || s[i] == '\'')) { if (s[i] >= '0' && s[i] <= '9') ++digits; ++i; }
    if (i < s.size() && s[i] == '.') { ++i; while (i < s.size() && s[i] >= '0' && s[i] <= '9') { ++digits; ++i; } }
    if (digits == 0) return false;

    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
        std::size_t exp_digits = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') { ++exp_digits; ++i; }
        if (exp_digits == 0) return false;
    }

    return i == s.size();
}

inline walnut_rt::f2048 parse_float(const std::string& s) {
    walnut_rt::special_kind k;
    if (special_word(s, k)) return walnut_rt::float_special<walnut_rt::f2048>(k);
    if (!parse_float_ok(s)) return walnut_rt::f2048::undefined();
    std::string clean;
    for (char c : s) if (c != '_' && c != '\'') clean += c;
    return walnut_rt::f2048(clean);
}

inline bool& unbuffered_request() noexcept {
    static bool requested = false;
    return requested;
}

inline bool start_unbuffered() {
    if (unbuffered_request()) return true;
    const char* e = std::getenv("WALNUT_UNBUFFERED");
    return e && *e && !(e[0] == '0' && e[1] == '\0');
}

} // namespace io_native
} // namespace walnut_rt

#endif // WALNUT_IO_HPP
