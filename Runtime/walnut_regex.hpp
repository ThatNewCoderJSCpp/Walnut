#ifndef WALNUT_REGEX_HPP
#define WALNUT_REGEX_HPP

#include "walnut_runtime.hpp"
#include "fizmo/../../Source/String/regex.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace walnut_rt {
namespace rx {

template <typename V, typename X>
inline void push_native(V& out, X&& value) {
    using T = typename V::value_type;
    if constexpr (std::is_constructible_v<T, X&&>) out.push_back(T(std::forward<X>(value)));
    else out.push_back(T(walnut_rt::native_tag{}, std::forward<X>(value)));
}

inline long long code_points_before(const std::string& s, std::size_t byte) {
    long long n = 0;
    for (std::size_t i = 0; i < byte && i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++n;
    }
    return n;
}

inline std::size_t byte_of(const std::string& s, long long index) {
    if (index <= 0) return 0;
    std::size_t i = 0;
    while (i < s.size() && index > 0) {
        ++i;
        while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
        --index;
    }
    return i;
}

class match {
public:
    match() = default;

    match(std::string subject, const fizmo::RegexMatch& m, std::vector<std::string> names) : m_subject(std::move(subject)), m_names(std::move(names)), m_found(true) {
        for (std::size_t g = 0; g < m.group_count(); ++g) m_spans.emplace_back(m.matched(g) ? m.begin(g) : npos, m.matched(g) ? m.end(g) : npos);
    }

    bool found() const noexcept { return m_found; }
    std::string text() const { return group(0); }
    std::string value() const { return group(0); }

    std::string group(long long index) const {
        if (!has_group(index)) return std::string();
        const auto& s = m_spans[static_cast<std::size_t>(index)];
        return m_subject.substr(s.first, s.second - s.first);
    }

    std::string named(const std::string& name) const {
        const long long i = index_of(name);
        return i < 0 ? std::string() : group(i);
    }

    bool has_group(long long index) const noexcept {
        return index >= 0 && static_cast<std::size_t>(index) < m_spans.size() && m_spans[static_cast<std::size_t>(index)].first != npos;
    }

    bool has_named(const std::string& name) const noexcept { return has_group(index_of(name)); }
    long long group_count() const noexcept { return static_cast<long long>(m_spans.size()); }
    long long start() const { return group_start(0); }
    long long end() const { return group_end(0); }
    long long stop() const { return group_end(0); }
    long long length() const { return end() - start(); }

    long long group_start(long long index) const {
        if (!has_group(index)) return -1;
        return code_points_before(m_subject, m_spans[static_cast<std::size_t>(index)].first);
    }

    long long group_end(long long index) const {
        if (!has_group(index)) return -1;
        return code_points_before(m_subject, m_spans[static_cast<std::size_t>(index)].second);
    }

    long long group_stop(long long index) const { return group_end(index); }

    std::string before() const { return m_found ? m_subject.substr(0, m_spans[0].first) : std::string(); }
    std::string after() const { return m_found ? m_subject.substr(m_spans[0].second) : std::string(); }

private:
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    long long index_of(const std::string& name) const noexcept {
        for (std::size_t i = 1; i < m_names.size(); ++i) {
            if (m_names[i] == name) return static_cast<long long>(i);
        }
        return -1;
    }

    std::string m_subject;
    std::vector<std::string> m_names;
    std::vector<std::pair<std::size_t, std::size_t>> m_spans;
    bool m_found = false;
};

class regex {
public:
    regex() = default;
    regex(const std::string& pattern) : m_re(pattern) {}
    regex(const std::string& pattern, long long flags) : m_re(pattern, static_cast<fizmo::RegexFlags>(static_cast<unsigned>(flags))) {}
    explicit regex(fizmo::Regex re) : m_re(std::move(re)) {}

    bool ok() const noexcept { return m_re.ok(); }
    std::string error() const { return m_re.error(); }
    long long error_position() const { return code_points_before(m_re.pattern(), m_re.error_position()); }
    std::string pattern() const { return m_re.pattern(); }
    long long group_count() const noexcept { return static_cast<long long>(m_re.group_count()); }
    bool gave_up() const noexcept { return m_re.gave_up(); }
    void set_step_limit(long long steps) { m_re.set_step_limit(steps < 1 ? 1 : static_cast<std::size_t>(steps)); }

    bool matches(const std::string& text) const { return m_re.matches(text); }
    bool contains(const std::string& text) const { return m_re.contains(text); }

    match whole(const std::string& text) const { return wrap(text, m_re.match(text)); }
    match search(const std::string& text) const { return wrap(text, m_re.search(text)); }
    match search_from(const std::string& text, long long start) const { return wrap(text, m_re.search(text, byte_of(text, start))); }
    match match_at(const std::string& text, long long position) const { return wrap(text, m_re.match_at(text, byte_of(text, position))); }

    template <typename V>
    void collect(const std::string& text, V& out) const {
        for (const fizmo::RegexMatch& m : m_re.find_all(text)) push_native(out, match(text, m, m_re.group_names()));
    }

    long long count(const std::string& text) const { return static_cast<long long>(m_re.count(text)); }
    std::string replace(const std::string& text, const std::string& replacement) const { return m_re.replace(text, replacement); }
    std::string replace_first(const std::string& text, const std::string& replacement) const { return m_re.replace_first(text, replacement); }
    std::string replace_count(const std::string& text, const std::string& replacement, long long limit) const { return m_re.replace(text, replacement, limit < 0 ? fizmo::Regex::npos : static_cast<std::size_t>(limit)); }

    template <typename V>
    void split_into(const std::string& text, V& out) const {
        for (std::string& part : m_re.split(text)) push_native(out, std::move(part));
    }

    std::string explain() const { return m_re.explain(); }

    static std::string escape(const std::string& literal) { return fizmo::Regex::escape(literal); }

    const fizmo::Regex& native() const noexcept { return m_re; }

private:
    match wrap(const std::string& text, const std::optional<fizmo::RegexMatch>& m) const { return m ? match(text, *m, m_re.group_names()) : match(); }

    fizmo::Regex m_re;
};

class charset {
public:
    charset() = default;
    explicit charset(fizmo::CharSet set) : m_set(std::move(set)) {}

    static charset everything() { return charset(fizmo::CharSet::everything()); }
    static charset nothing() { return charset(fizmo::CharSet::nothing()); }
    static charset of(const std::string& chars) { return charset(fizmo::CharSet::of(chars)); }
    static charset between(char32_t first, char32_t last) { return charset(fizmo::CharSet::range(first, last)); }
    static charset digits() { return charset(fizmo::CharSet::digits()); }
    static charset numbers() { return charset(fizmo::CharSet::numbers()); }
    static charset letters() { return charset(fizmo::CharSet::letters()); }
    static charset uppercase() { return charset(fizmo::CharSet::uppercase()); }
    static charset lowercase() { return charset(fizmo::CharSet::lowercase()); }
    static charset spaces() { return charset(fizmo::CharSet::spaces()); }
    static charset punctuation() { return charset(fizmo::CharSet::punctuation()); }
    static charset symbols() { return charset(fizmo::CharSet::symbols()); }
    static charset special() { return charset(fizmo::CharSet::special()); }
    static charset special_characters() { return charset(fizmo::CharSet::special()); }
    static charset emoji() { return charset(fizmo::CharSet::emoji()); }
    static charset hex_digits() { return charset(fizmo::CharSet::hex_digits()); }
    static charset word_characters() { return charset(fizmo::CharSet::word_characters()); }
    static charset ascii() { return charset(fizmo::CharSet::ascii()); }

    charset& allow(const std::string& chars) { m_set.allow(chars); return *this; }
    charset& disable(const std::string& chars) { m_set.disable(chars); return *this; }
    charset& allow_char(char32_t c) { m_set.allow_range(c, c); return *this; }
    charset& disable_char(char32_t c) { m_set.disable_range(c, c); return *this; }
    charset& allow_range(char32_t first, char32_t last) { m_set.allow_range(first, last); return *this; }
    charset& disable_range(char32_t first, char32_t last) { m_set.disable_range(first, last); return *this; }
    charset& allow_set(const charset& other) { m_set.allow(other.m_set); return *this; }
    charset& disable_set(const charset& other) { m_set.disable(other.m_set); return *this; }
    charset& allow_digits() { m_set.allow_digits(); return *this; }
    charset& disable_digits() { m_set.disable_digits(); return *this; }
    charset& allow_numbers() { m_set.allow_numbers(); return *this; }
    charset& disable_numbers() { m_set.disable_numbers(); return *this; }
    charset& allow_letters() { m_set.allow_letters(); return *this; }
    charset& disable_letters() { m_set.disable_letters(); return *this; }
    charset& allow_uppercase() { m_set.allow_uppercase(); return *this; }
    charset& disable_uppercase() { m_set.disable_uppercase(); return *this; }
    charset& allow_lowercase() { m_set.allow_lowercase(); return *this; }
    charset& disable_lowercase() { m_set.disable_lowercase(); return *this; }
    charset& allow_spaces() { m_set.allow_spaces(); return *this; }
    charset& disable_spaces() { m_set.disable_spaces(); return *this; }
    charset& allow_newlines() { m_set.allow_newlines(); return *this; }
    charset& disable_newlines() { m_set.disable_newlines(); return *this; }
    charset& allow_punctuation() { m_set.allow_punctuation(); return *this; }
    charset& disable_punctuation() { m_set.disable_punctuation(); return *this; }
    charset& disable_punctuation_except(const std::string& keep) { m_set.disable_punctuation(keep); return *this; }
    charset& allow_symbols() { m_set.allow_symbols(); return *this; }
    charset& disable_symbols() { m_set.disable_symbols(); return *this; }
    charset& disable_symbols_except(const std::string& keep) { m_set.disable_symbols(keep); return *this; }
    charset& allow_special() { m_set.allow_special(); return *this; }
    charset& disable_special() { m_set.disable_special(); return *this; }
    charset& disable_special_except(const std::string& keep) { m_set.disable_special(keep); return *this; }
    charset& allow_emoji() { m_set.allow_emoji(); return *this; }
    charset& disable_emoji() { m_set.disable_emoji(); return *this; }
    charset& allow_control() { m_set.allow_control(); return *this; }
    charset& disable_control() { m_set.disable_control(); return *this; }
    charset& only_ascii() { m_set.only_ascii(); return *this; }
    charset& allow_hex_digits() { m_set.allow_hex_digits(); return *this; }

    bool contains(char32_t c) const noexcept { return m_set.contains(c); }
    bool accepts(const std::string& text) const noexcept { return m_set.accepts(text); }

    long long first_rejected(const std::string& text) const {
        const std::size_t at = m_set.first_rejected(text);
        return at == fizmo::StringView::npos ? -1 : code_points_before(text, at);
    }

    std::string filter(const std::string& text) const { return m_set.filter(text); }
    charset inverted() const { return charset(m_set.inverted()); }
    std::string to_regex() const { return m_set.to_regex(); }
    std::string describe() const { return m_set.describe(); }

    const fizmo::CharSet& native() const noexcept { return m_set; }

private:
    fizmo::CharSet m_set;
};

class pattern {
public:
    pattern() = default;
    explicit pattern(fizmo::Pattern p) : m_p(std::move(p)) {}

    static pattern literal(const std::string& text) { return pattern(fizmo::Pattern::text(text)); }
    static pattern one_of(const charset& set) { return pattern(fizmo::Pattern::one_of(set.native())); }
    static pattern one_of_chars(const std::string& chars) { return pattern(fizmo::Pattern::one_of(chars)); }
    static pattern not_one_of(const std::string& chars) { return pattern(fizmo::Pattern::not_one_of(chars)); }
    static pattern any_character() { return pattern(fizmo::Pattern::any_character()); }
    static pattern any_except_newline() { return pattern(fizmo::Pattern::any_except_newline()); }
    static pattern digit() { return pattern(fizmo::Pattern::digit()); }
    static pattern letter() { return pattern(fizmo::Pattern::letter()); }
    static pattern whitespace() { return pattern(fizmo::Pattern::whitespace()); }
    static pattern word_character() { return pattern(fizmo::Pattern::word_character()); }
    static pattern start_of_text() { return pattern(fizmo::Pattern::start_of_text()); }
    static pattern end_of_text() { return pattern(fizmo::Pattern::end_of_text()); }
    static pattern start_of_line() { return pattern(fizmo::Pattern::start_of_line()); }
    static pattern end_of_line() { return pattern(fizmo::Pattern::end_of_line()); }
    static pattern word_boundary() { return pattern(fizmo::Pattern::word_boundary()); }
    static pattern either(const pattern& a, const pattern& b) { return pattern(fizmo::Pattern::either({ a.m_p, b.m_p })); }
    static pattern same_as(const std::string& group_name) { return pattern(fizmo::Pattern::same_as(group_name)); }
    static pattern same_as_group(long long number) { return pattern(fizmo::Pattern::same_as(static_cast<std::size_t>(number < 0 ? 0 : number))); }
    static pattern raw(const std::string& regex) { return pattern(fizmo::Pattern::raw(regex)); }
    static pattern digits(long long at_least) { return pattern(fizmo::Pattern::digits(count(at_least))); }
    static pattern integer() { return pattern(fizmo::Pattern::integer()); }
    static pattern integer_number() { return pattern(fizmo::Pattern::integer()); }
    static pattern unsigned_integer() { return pattern(fizmo::Pattern::integer(false)); }
    static pattern decimal() { return pattern(fizmo::Pattern::decimal()); }
    static pattern decimal_number() { return pattern(fizmo::Pattern::decimal()); }
    static pattern identifier() { return pattern(fizmo::Pattern::identifier()); }
    static pattern hex_number() { return pattern(fizmo::Pattern::hex_number()); }
    static pattern hex_color() { return pattern(fizmo::Pattern::hex_color()); }
    static pattern email() { return pattern(fizmo::Pattern::email()); }
    static pattern word() { return pattern(fizmo::Pattern::word()); }

    pattern and_then(const pattern& next) const { fizmo::Pattern p = m_p; p.then(next.m_p); return pattern(p); }
    pattern and_then_literal(const std::string& text) const { fizmo::Pattern p = m_p; p.then(text); return pattern(p); }
    pattern or_else(const pattern& other) const { fizmo::Pattern p = m_p; p.or_else(other.m_p); return pattern(p); }
    pattern optional() const { return pattern(m_p.optional()); }
    pattern one_or_more() const { return pattern(m_p.one_or_more()); }
    pattern zero_or_more() const { return pattern(m_p.zero_or_more()); }
    pattern times(long long n) const { return pattern(m_p.repeat(count(n))); }
    pattern times_between(long long min, long long max) const { return pattern(m_p.repeat(count(min), count(max))); }
    pattern at_least(long long n) const { return pattern(m_p.at_least(count(n))); }
    pattern at_most(long long n) const { return pattern(m_p.at_most(count(n))); }
    pattern as_few_as_possible() const { return pattern(m_p.lazy()); }
    pattern no_backtracking() const { return pattern(m_p.possessive()); }
    pattern capture() const { return pattern(m_p.capture()); }
    pattern capture_as(const std::string& name) const { return pattern(m_p.capture(name)); }
    pattern group() const { return pattern(m_p.group()); }
    pattern followed_by(const pattern& next) const { return pattern(m_p.followed_by(next.m_p)); }
    pattern not_followed_by(const pattern& next) const { return pattern(m_p.not_followed_by(next.m_p)); }
    pattern preceded_by(const pattern& before) const { return pattern(m_p.preceded_by(before.m_p)); }
    pattern not_preceded_by(const pattern& before) const { return pattern(m_p.not_preceded_by(before.m_p)); }
    pattern whole() const { return pattern(m_p.whole()); }

    std::string to_regex() const { return m_p.to_regex(); }
    regex compile() const { return regex(m_p.compile()); }
    regex compile_with(long long flags) const { return regex(m_p.compile(static_cast<fizmo::RegexFlags>(static_cast<unsigned>(flags)))); }
    bool matches(const std::string& text) const { return m_p.matches(text); }
    std::string explain() const { return m_p.explain(); }

private:
    static std::size_t count(long long n) { return n < 0 ? 0 : static_cast<std::size_t>(n); }

    fizmo::Pattern m_p;
};

} // namespace rx
} // namespace walnut_rt

#endif // WALNUT_REGEX_HPP
