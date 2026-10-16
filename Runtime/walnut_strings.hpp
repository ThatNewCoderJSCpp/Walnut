#ifndef WALNUT_STRINGS_HPP
#define WALNUT_STRINGS_HPP

#include "walnut_runtime.hpp"
#include "fizmo/../../Source/String/unicode.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace walnut_rt {
namespace str {

struct tag {};

template <typename... T>
struct ttag {};

using cps = std::vector<char32_t>;

inline cps decode(const std::string& s) { return decode_utf8(s); }

inline std::string encode(const cps& v, std::size_t from = 0, std::size_t to = static_cast<std::size_t>(-1)) {
    std::string out;
    if (to > v.size()) to = v.size();
    for (std::size_t i = from; i < to; ++i) append_utf8(out, v[i]);
    return out;
}

inline std::string text_of(const std::string& s) { return s; }
inline std::string text_of(char32_t c) { return utf8(c); }

inline i64 idx(long long n) { return i64(n); }

inline i64 cp_at(const std::string& s, std::size_t byte) {
    long long n = 0;
    for (std::size_t i = 0; i < byte && i < s.size(); ++i) if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++n;
    return i64(n);
}

inline std::size_t byte_at(const std::string& s, long long n) {
    if (n <= 0) return 0;
    std::size_t i = 0;
    while (i < s.size() && n > 0) {
        ++i;
        while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
        --n;
    }
    return i;
}

inline bool is_space_cp(char32_t c) { return fizmo::unicode::is_whitespace(c); }

inline std::string upper_full(char32_t c) {
    char32_t out[3];
    const std::size_t n = fizmo::unicode::full_to_upper(c, out);
    std::string r;
    for (std::size_t i = 0; i < n; ++i) append_utf8(r, out[i]);
    return r;
}

inline std::string lower_full(char32_t c) {
    char32_t out[3];
    const std::size_t n = fizmo::unicode::full_to_lower(c, out);
    std::string r;
    for (std::size_t i = 0; i < n; ++i) append_utf8(r, out[i]);
    return r;
}

inline std::string title_full(char32_t c) {
    char32_t out[3];
    const std::size_t n = fizmo::unicode::full_to_title(c, out);
    std::string r;
    for (std::size_t i = 0; i < n; ++i) append_utf8(r, out[i]);
    return r;
}

inline bool is_digit(char32_t c) { return fizmo::unicode::is_digit(c); }
inline bool is_alpha(char32_t c) { return fizmo::unicode::is_alphabetic(c); }
inline bool is_alnum(char32_t c) { return fizmo::unicode::is_alphanumeric(c); }
inline bool is_space(char32_t c) { return fizmo::unicode::is_whitespace(c); }
inline bool is_upper(char32_t c) { return fizmo::unicode::is_uppercase(c); }
inline bool is_lower(char32_t c) { return fizmo::unicode::is_lowercase(c); }
inline bool is_punct(char32_t c) { return fizmo::unicode::is_punctuation(c); }
inline bool is_letter(char32_t c) { return fizmo::unicode::is_letter(c); }
inline bool is_ascii_char(char32_t c) { return c < 0x80; }
inline bool is_hex_digit(char32_t c) { return (c >= U'0' && c <= U'9') || (c >= U'a' && c <= U'f') || (c >= U'A' && c <= U'F'); }
inline char32_t upper_char(char32_t c) { return fizmo::unicode::to_upper(c); }
inline char32_t lower_char(char32_t c) { return fizmo::unicode::to_lower(c); }

template <typename N, typename I>
inline i64 find_from(const std::string& s, const N& needle, const I& from) {
    const long long start = to_ll(from);
    if (start < 0) return i64(-1LL);
    const std::size_t b = byte_at(s, start);
    if (b > s.size()) return i64(-1LL);
    const std::size_t at = s.find(text_of(needle), b);
    return at == std::string::npos ? i64(-1LL) : cp_at(s, at);
}

template <typename N>
inline i64 rfind(const std::string& s, const N& needle) {
    const std::size_t at = s.rfind(text_of(needle));
    return at == std::string::npos ? i64(-1LL) : cp_at(s, at);
}

template <typename N, typename I>
inline i64 find_nth(const std::string& s, const N& needle, const I& nth) {
    const std::string n = text_of(needle);
    long long want = to_ll(nth);
    if (n.empty() || want < 0) return i64(-1LL);
    std::size_t at = s.find(n);
    while (at != std::string::npos) {
        if (want == 0) return cp_at(s, at);
        --want;
        at = s.find(n, at + n.size());
    }
    return i64(-1LL);
}

template <typename N, typename V>
inline void find_all(const std::string& s, const N& needle, V& out) {
    const std::string n = text_of(needle);
    if (n.empty()) return;
    std::size_t at = s.find(n);
    while (at != std::string::npos) {
        out.push_back(cp_at(s, at));
        at = s.find(n, at + n.size());
    }
}

template <typename N>
inline i64 count(const std::string& s, const N& needle) {
    const std::string n = text_of(needle);
    if (n.empty()) return i64(0LL);
    long long total = 0;
    std::size_t at = s.find(n);
    while (at != std::string::npos) {
        ++total;
        at = s.find(n, at + n.size());
    }
    return i64(total);
}

inline bool in_set(char32_t c, const cps& set) { return std::find(set.begin(), set.end(), c) != set.end(); }

inline i64 find_first_of(const std::string& s, const std::string& chars) {
    const cps v = decode(s), set = decode(chars);
    for (std::size_t i = 0; i < v.size(); ++i) if (in_set(v[i], set)) return i64(static_cast<long long>(i));
    return i64(-1LL);
}

inline i64 find_last_of(const std::string& s, const std::string& chars) {
    const cps v = decode(s), set = decode(chars);
    for (std::size_t i = v.size(); i > 0; --i) if (in_set(v[i - 1], set)) return i64(static_cast<long long>(i - 1));
    return i64(-1LL);
}

inline i64 find_first_not_of(const std::string& s, const std::string& chars) {
    const cps v = decode(s), set = decode(chars);
    for (std::size_t i = 0; i < v.size(); ++i) if (!in_set(v[i], set)) return i64(static_cast<long long>(i));
    return i64(-1LL);
}

inline i64 find_last_not_of(const std::string& s, const std::string& chars) {
    const cps v = decode(s), set = decode(chars);
    for (std::size_t i = v.size(); i > 0; --i) if (!in_set(v[i - 1], set)) return i64(static_cast<long long>(i - 1));
    return i64(-1LL);
}

inline bool contains_any(const std::string& s, const std::string& chars) { return to_ll(find_first_of(s, chars)) >= 0; }

template <typename N, typename V>
inline void split(const std::string& s, const N& separator, V& out) {
    const std::string sep = text_of(separator);
    if (sep.empty()) {
        for (char32_t c : decode(s)) out.push_back(utf8(c));
        return;
    }
    std::size_t start = 0;
    std::size_t at = s.find(sep);
    while (at != std::string::npos) {
        out.push_back(s.substr(start, at - start));
        start = at + sep.size();
        at = s.find(sep, start);
    }
    out.push_back(s.substr(start));
}

template <typename N, typename I, typename V>
inline void split_n(const std::string& s, const N& separator, const I& limit, V& out) {
    const std::string sep = text_of(separator);
    long long left = to_ll(limit);
    if (sep.empty() || left <= 1) { out.push_back(s); return; }
    std::size_t start = 0;
    std::size_t at = s.find(sep);
    while (at != std::string::npos && left > 1) {
        out.push_back(s.substr(start, at - start));
        start = at + sep.size();
        at = s.find(sep, start);
        --left;
    }
    out.push_back(s.substr(start));
}

template <typename V>
inline void split_whitespace(const std::string& s, V& out) {
    const cps v = decode(s);
    std::size_t i = 0;
    while (i < v.size()) {
        while (i < v.size() && is_space_cp(v[i])) ++i;
        std::size_t j = i;
        while (j < v.size() && !is_space_cp(v[j])) ++j;
        if (j > i) out.push_back(encode(v, i, j));
        i = j;
    }
}

template <typename V>
inline void split_any(const std::string& s, const std::string& chars, V& out) {
    const cps v = decode(s), set = decode(chars);
    std::size_t start = 0;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (in_set(v[i], set)) { out.push_back(encode(v, start, i)); start = i + 1; }
    }
    out.push_back(encode(v, start, v.size()));
}

template <typename V>
inline void lines(const std::string& s, V& out) {
    std::size_t start = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\n') {
            std::size_t stop = i;
            if (stop > start && s[stop - 1] == '\r') --stop;
            out.push_back(s.substr(start, stop - start));
            start = i + 1;
        }
    }
    if (start < s.size()) {
        std::size_t stop = s.size();
        if (stop > start && s[stop - 1] == '\r') --stop;
        out.push_back(s.substr(start, stop - start));
    }
}

template <typename V>
inline void words(const std::string& s, V& out) {
    const cps v = decode(s);
    std::size_t i = 0;
    while (i < v.size()) {
        while (i < v.size() && !(fizmo::unicode::is_alphanumeric(v[i]) || v[i] == U'_' || v[i] == U'\'')) ++i;
        std::size_t j = i;
        while (j < v.size() && (fizmo::unicode::is_alphanumeric(v[j]) || v[j] == U'_' || v[j] == U'\'')) ++j;
        if (j > i) out.push_back(encode(v, i, j));
        i = j;
    }
}

template <typename I, typename V>
inline void chunks(const std::string& s, const I& size, V& out) {
    const long long n = to_ll(size);
    if (n <= 0) fail("chunks() needs a size of at least 1, not " + std::to_string(n));
    const cps v = decode(s);
    for (std::size_t i = 0; i < v.size(); i += static_cast<std::size_t>(n)) out.push_back(encode(v, i, i + static_cast<std::size_t>(n)));
}

template <typename V>
inline void chars(const std::string& s, V& out) {
    for (char32_t c : decode(s)) out.push_back(c);
}

template <typename V, typename N>
inline std::string join(const V& parts, const N& separator) {
    const std::string sep = text_of(separator);
    std::string out;
    bool first = true;
    for (const auto& p : parts) {
        if (!first) out += sep;
        out += p;
        first = false;
    }
    return out;
}

template <typename A, typename B, typename I>
inline std::string replace_n(const std::string& s, const A& from, const B& to, const I& limit) {
    const std::string f = text_of(from), t = text_of(to);
    long long left = to_ll(limit);
    if (f.empty() || left == 0) return s;
    std::string out;
    std::size_t start = 0;
    std::size_t at = s.find(f);
    while (at != std::string::npos && left != 0) {
        out.append(s, start, at - start);
        out += t;
        start = at + f.size();
        at = s.find(f, start);
        if (left > 0) --left;
    }
    out.append(s, start, std::string::npos);
    return out;
}

template <typename A, typename B>
inline std::string replace(const std::string& s, const A& from, const B& to) { return replace_n(s, from, to, -1LL); }

template <typename A, typename B>
inline std::string replace_first(const std::string& s, const A& from, const B& to) { return replace_n(s, from, to, 1LL); }

template <typename A, typename B>
inline std::string replace_last(const std::string& s, const A& from, const B& to) {
    const std::string f = text_of(from), t = text_of(to);
    if (f.empty()) return s;
    const std::size_t at = s.rfind(f);
    if (at == std::string::npos) return s;
    return s.substr(0, at) + t + s.substr(at + f.size());
}

template <typename N>
inline std::string remove(const std::string& s, const N& needle) { return replace_n(s, needle, std::string(), -1LL); }

template <typename N>
inline std::string remove_prefix(const std::string& s, const N& prefix) {
    const std::string p = text_of(prefix);
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0 ? s.substr(p.size()) : s;
}

template <typename N>
inline std::string remove_suffix(const std::string& s, const N& suffix) {
    const std::string p = text_of(suffix);
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0 ? s.substr(0, s.size() - p.size()) : s;
}

inline std::string trim_left(const std::string& s) {
    const cps v = decode(s);
    std::size_t i = 0;
    while (i < v.size() && is_space_cp(v[i])) ++i;
    return encode(v, i, v.size());
}

inline std::string trim_right(const std::string& s) {
    const cps v = decode(s);
    std::size_t j = v.size();
    while (j > 0 && is_space_cp(v[j - 1])) --j;
    return encode(v, 0, j);
}

inline std::string trim_all(const std::string& s) { return trim_left(trim_right(s)); }

inline std::string trim_chars(const std::string& s, const std::string& chars) {
    const cps v = decode(s), set = decode(chars);
    std::size_t i = 0, j = v.size();
    while (i < j && in_set(v[i], set)) ++i;
    while (j > i && in_set(v[j - 1], set)) --j;
    return encode(v, i, j);
}

inline std::string upper(const std::string& s) {
    std::string out;
    for (char32_t c : decode(s)) out += upper_full(c);
    return out;
}

inline std::string lower(const std::string& s) {
    std::string out;
    for (char32_t c : decode(s)) out += lower_full(c);
    return out;
}

inline std::string capitalize(const std::string& s) {
    const cps v = decode(s);
    if (v.empty()) return s;
    std::string out = title_full(v[0]);
    for (std::size_t i = 1; i < v.size(); ++i) out += lower_full(v[i]);
    return out;
}

inline std::string title_case(const std::string& s) {
    std::string out;
    bool start = true;
    for (char32_t c : decode(s)) {
        if (fizmo::unicode::is_alphanumeric(c) || c == U'\'') {
            out += start ? title_full(c) : lower_full(c);
            start = false;
        } else {
            append_utf8(out, c);
            start = true;
        }
    }
    return out;
}

inline std::string swap_case(const std::string& s) {
    std::string out;
    for (char32_t c : decode(s)) {
        if (fizmo::unicode::is_uppercase(c)) out += lower_full(c);
        else if (fizmo::unicode::is_lowercase(c)) out += upper_full(c);
        else append_utf8(out, c);
    }
    return out;
}

inline std::string reverse(const std::string& s) {
    cps v = decode(s);
    std::reverse(v.begin(), v.end());
    return encode(v);
}

template <typename I>
inline std::string repeated(const std::string& s, const I& times) {
    const long long n = to_ll(times);
    std::string out;
    for (long long k = 0; k < n; ++k) out += s;
    return out;
}

template <typename I>
inline std::string pad_left(const std::string& s, const I& width, char32_t fill) {
    const long long have = static_cast<long long>(decode(s).size());
    const long long want = to_ll(width);
    std::string out;
    for (long long k = have; k < want; ++k) append_utf8(out, fill);
    return out + s;
}

template <typename I>
inline std::string pad_right(const std::string& s, const I& width, char32_t fill) {
    const long long have = static_cast<long long>(decode(s).size());
    const long long want = to_ll(width);
    std::string out = s;
    for (long long k = have; k < want; ++k) append_utf8(out, fill);
    return out;
}

template <typename I>
inline std::string center(const std::string& s, const I& width, char32_t fill) {
    const long long have = static_cast<long long>(decode(s).size());
    const long long want = to_ll(width);
    if (want <= have) return s;
    const long long left = (want - have) / 2;
    std::string out;
    for (long long k = 0; k < left; ++k) append_utf8(out, fill);
    out += s;
    for (long long k = have + left; k < want; ++k) append_utf8(out, fill);
    return out;
}

template <typename I>
inline std::string zfill(const std::string& s, const I& width) {
    const bool sign = !s.empty() && (s[0] == '-' || s[0] == '+');
    const std::string body = sign ? s.substr(1) : s;
    const long long have = static_cast<long long>(decode(s).size());
    const long long want = to_ll(width);
    std::string out = sign ? s.substr(0, 1) : std::string();
    for (long long k = have; k < want; ++k) out += '0';
    return out + body;
}

template <typename I>
inline std::string insert(const std::string& s, const I& index, const std::string& piece) {
    const long long n = to_ll(index);
    const long long total = static_cast<long long>(decode(s).size());
    if (n < 0 || n > total) fail("insert position " + std::to_string(n) + " out of range for a string of length " + std::to_string(total));
    const std::size_t b = byte_at(s, n);
    return s.substr(0, b) + piece + s.substr(b);
}

template <typename I, typename J>
inline std::string erase(const std::string& s, const I& index, const J& count) {
    const long long n = to_ll(index);
    const long long c = to_ll(count);
    const long long total = static_cast<long long>(decode(s).size());
    if (n < 0 || n > total) fail("erase position " + std::to_string(n) + " out of range for a string of length " + std::to_string(total));
    if (c < 0) fail("erase count " + std::to_string(c) + " is negative");
    const std::size_t b = byte_at(s, n);
    const std::size_t e = byte_at(s, n + c);
    return s.substr(0, b) + s.substr(std::min(e, s.size()));
}

inline long long clamp_index(long long i, long long total) {
    if (i < 0) i += total;
    if (i < 0) i = 0;
    if (i > total) i = total;
    return i;
}

template <typename I, typename J>
inline std::string slice(const std::string& s, const I& start, const J& stop) {
    const cps v = decode(s);
    const long long total = static_cast<long long>(v.size());
    const long long a = clamp_index(to_ll(start), total);
    const long long b = clamp_index(to_ll(stop), total);
    if (b <= a) return std::string();
    return encode(v, static_cast<std::size_t>(a), static_cast<std::size_t>(b));
}

template <typename I>
inline std::string left(const std::string& s, const I& count) {
    const long long n = to_ll(count);
    if (n <= 0) return std::string();
    return s.substr(0, byte_at(s, n));
}

template <typename I>
inline std::string right(const std::string& s, const I& count) {
    const cps v = decode(s);
    const long long n = to_ll(count);
    if (n <= 0) return std::string();
    const std::size_t from = static_cast<long long>(v.size()) > n ? v.size() - static_cast<std::size_t>(n) : 0;
    return encode(v, from, v.size());
}

template <typename I>
inline std::string truncate(const std::string& s, const I& width, const std::string& ellipsis) {
    const cps v = decode(s);
    const long long n = to_ll(width);
    if (static_cast<long long>(v.size()) <= n) return s;
    const long long e = static_cast<long long>(decode(ellipsis).size());
    if (n <= e) return encode(decode(ellipsis), 0, static_cast<std::size_t>(n < 0 ? 0 : n));
    return encode(v, 0, static_cast<std::size_t>(n - e)) + ellipsis;
}

template <typename I>
inline std::string indent(const std::string& s, const I& count) {
    const long long n = to_ll(count);
    const std::string pad(static_cast<std::size_t>(n < 0 ? 0 : n), ' ');
    std::string out;
    bool line_start = true;
    for (char c : s) {
        if (line_start && c != '\n') out += pad;
        out += c;
        line_start = c == '\n';
    }
    return out;
}

template <typename I>
inline std::string wrap(const std::string& s, const I& width) {
    const long long w = to_ll(width);
    if (w <= 0) fail("wrap() needs a width of at least 1, not " + std::to_string(w));
    std::vector<std::string> paragraphs;
    lines(s, paragraphs);
    std::string out;
    for (std::size_t p = 0; p < paragraphs.size(); ++p) {
        if (p) out += '\n';
        std::vector<std::string> ws;
        split_whitespace(paragraphs[p], ws);
        long long used = 0;
        for (const std::string& word : ws) {
            const long long len = static_cast<long long>(decode(word).size());
            if (used > 0 && used + 1 + len > w) { out += '\n'; used = 0; }
            if (used > 0) { out += ' '; ++used; }
            out += word;
            used += len;
        }
    }
    return out;
}

inline std::string normalize_whitespace(const std::string& s) {
    std::vector<std::string> ws;
    split_whitespace(s, ws);
    return join(ws, std::string(" "));
}

inline std::string camel_to_snake(const std::string& s) {
    const cps v = decode(s);
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        const char32_t c = v[i];
        if (fizmo::unicode::is_uppercase(c)) {
            const bool after_lower = i > 0 && (fizmo::unicode::is_lowercase(v[i - 1]) || fizmo::unicode::is_digit(v[i - 1]));
            const bool before_lower = i > 0 && i + 1 < v.size() && fizmo::unicode::is_uppercase(v[i - 1]) && fizmo::unicode::is_lowercase(v[i + 1]);
            if (after_lower || before_lower) out += '_';
            out += lower_full(c);
        } else if (c == U' ' || c == U'-') {
            out += '_';
        } else {
            append_utf8(out, c);
        }
    }
    return out;
}

inline std::string snake_to_camel(const std::string& s) {
    std::string out;
    bool up = false;
    bool first = true;
    for (char32_t c : decode(s)) {
        if (c == U'_' || c == U'-' || c == U' ') { up = !first; continue; }
        if (up) { out += upper_full(c); up = false; }
        else append_utf8(out, c);
        first = false;
    }
    return out;
}

inline std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

inline int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

inline std::string url_decode(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && hex_value(s[i + 1]) >= 0 && hex_value(s[i + 2]) >= 0) {
            out += static_cast<char>(hex_value(s[i + 1]) * 16 + hex_value(s[i + 2]));
            i += 2;
        } else if (s[i] == '+') {
            out += ' ';
        } else {
            out += s[i];
        }
    }
    return out;
}

inline std::string escape(const std::string& s) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\'': out += "\\'"; break;
            case '\0': out += "\\0"; break;
            default:
                if (c < 0x20 || c == 0x7F) { out += "\\x"; out += hex[c >> 4]; out += hex[c & 15]; }
                else out += static_cast<char>(c);
        }
    }
    return out;
}

inline std::string unescape(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) { out += s[i]; continue; }
        const char n = s[++i];
        switch (n) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case '0': out += '\0'; break;
            case 'x':
                if (i + 2 < s.size() && hex_value(s[i + 1]) >= 0 && hex_value(s[i + 2]) >= 0) {
                    out += static_cast<char>(hex_value(s[i + 1]) * 16 + hex_value(s[i + 2]));
                    i += 2;
                } else {
                    out += "\\x";
                }
                break;
            default: out += n; break;
        }
    }
    return out;
}

inline bool is_blank(const std::string& s) {
    for (char32_t c : decode(s)) if (!is_space_cp(c)) return false;
    return true;
}

template <typename P>
inline bool all_chars(const std::string& s, P pred) {
    const cps v = decode(s);
    if (v.empty()) return false;
    for (char32_t c : v) if (!pred(c)) return false;
    return true;
}

inline bool is_digits(const std::string& s) { return all_chars(s, [](char32_t c) { return c >= U'0' && c <= U'9'; }); }
inline bool is_alpha_text(const std::string& s) { return all_chars(s, [](char32_t c) { return fizmo::unicode::is_alphabetic(c); }); }
inline bool is_alnum_text(const std::string& s) { return all_chars(s, [](char32_t c) { return fizmo::unicode::is_alphanumeric(c); }); }
inline bool is_hex(const std::string& s) { return all_chars(s, [](char32_t c) { return is_hex_digit(c); }); }
inline bool is_ascii(const std::string& s) {
    for (unsigned char c : s) if (c >= 0x80) return false;
    return true;
}

inline bool is_upper_text(const std::string& s) {
    bool cased = false;
    for (char32_t c : decode(s)) {
        if (fizmo::unicode::is_lowercase(c)) return false;
        if (fizmo::unicode::is_uppercase(c)) cased = true;
    }
    return cased;
}

inline bool is_lower_text(const std::string& s) {
    bool cased = false;
    for (char32_t c : decode(s)) {
        if (fizmo::unicode::is_uppercase(c)) return false;
        if (fizmo::unicode::is_lowercase(c)) cased = true;
    }
    return cased;
}

inline bool is_integer(const std::string& s) {
    std::size_t i = 0;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
    if (i >= s.size()) return false;
    for (; i < s.size(); ++i) if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

inline bool is_number(const std::string& s) {
    std::size_t i = 0;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
    bool digits = false, dot = false;
    for (; i < s.size(); ++i) {
        if (s[i] >= '0' && s[i] <= '9') digits = true;
        else if (s[i] == '.' && !dot) dot = true;
        else break;
    }
    if (!digits) return false;
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
        bool exp = false;
        for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) exp = true;
        if (!exp) return false;
    }
    return i == s.size();
}

inline bool is_palindrome(const std::string& s) {
    cps v;
    for (char32_t c : decode(s)) if (fizmo::unicode::is_alphanumeric(c)) v.push_back(fizmo::unicode::fold_case(c));
    for (std::size_t i = 0, j = v.size(); i < j; ++i, --j) if (v[i] != v[j - 1]) return false;
    return true;
}

inline i64 levenshtein(const std::string& a, const std::string& b) {
    const cps x = decode(a), y = decode(b);
    std::vector<long long> prev(y.size() + 1), cur(y.size() + 1);
    for (std::size_t j = 0; j <= y.size(); ++j) prev[j] = static_cast<long long>(j);
    for (std::size_t i = 1; i <= x.size(); ++i) {
        cur[0] = static_cast<long long>(i);
        for (std::size_t j = 1; j <= y.size(); ++j) {
            const long long cost = x[i - 1] == y[j - 1] ? 0 : 1;
            cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost });
        }
        std::swap(prev, cur);
    }
    return i64(prev[y.size()]);
}

inline std::string common_prefix(const std::string& a, const std::string& b) {
    const cps x = decode(a), y = decode(b);
    std::size_t n = 0;
    while (n < x.size() && n < y.size() && x[n] == y[n]) ++n;
    return encode(x, 0, n);
}

inline std::string common_suffix(const std::string& a, const std::string& b) {
    const cps x = decode(a), y = decode(b);
    std::size_t n = 0;
    while (n < x.size() && n < y.size() && x[x.size() - 1 - n] == y[y.size() - 1 - n]) ++n;
    return encode(x, x.size() - n, x.size());
}

inline i64 word_count(const std::string& s) {
    std::vector<std::string> ws;
    words(s, ws);
    return i64(static_cast<long long>(ws.size()));
}

inline i64 line_count(const std::string& s) {
    std::vector<std::string> ls;
    lines(s, ls);
    return i64(static_cast<long long>(ls.size()));
}

inline std::string fold(const std::string& s) {
    std::string out;
    for (char32_t c : decode(s)) append_utf8(out, fizmo::unicode::fold_case(c));
    return out;
}

inline bool equals_ignore_case(const std::string& a, const std::string& b) { return fold(a) == fold(b); }

inline i64 compare_ignore_case(const std::string& a, const std::string& b) {
    const cps x = decode(fold(a)), y = decode(fold(b));
    const std::size_t n = std::min(x.size(), y.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (x[i] < y[i]) return i64(-1LL);
        if (x[i] > y[i]) return i64(1LL);
    }
    if (x.size() < y.size()) return i64(-1LL);
    if (x.size() > y.size()) return i64(1LL);
    return i64(0LL);
}

inline bool starts_with_ignore_case(const std::string& s, const std::string& p) {
    const std::string fs = fold(s), fp = fold(p);
    return fs.size() >= fp.size() && fs.compare(0, fp.size(), fp) == 0;
}

inline bool ends_with_ignore_case(const std::string& s, const std::string& p) {
    const std::string fs = fold(s), fp = fold(p);
    return fs.size() >= fp.size() && fs.compare(fs.size() - fp.size(), fp.size(), fp) == 0;
}

inline bool contains_ignore_case(const std::string& s, const std::string& p) { return fold(s).find(fold(p)) != std::string::npos; }

inline bool matches_glob(const std::string& s, const std::string& pattern) {
    const cps t = decode(s), p = decode(pattern);
    std::size_t i = 0, j = 0, star = static_cast<std::size_t>(-1), mark = 0;
    while (i < t.size()) {
        if (j < p.size() && (p[j] == U'?' || p[j] == t[i])) { ++i; ++j; }
        else if (j < p.size() && p[j] == U'*') { star = j++; mark = i; }
        else if (star != static_cast<std::size_t>(-1)) { j = star + 1; i = ++mark; }
        else return false;
    }
    while (j < p.size() && p[j] == U'*') ++j;
    return j == p.size();
}

} // namespace str
} // namespace walnut_rt

#endif // WALNUT_STRINGS_HPP
