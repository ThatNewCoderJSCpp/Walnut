#include "Lexer/special_names.hpp"
#include "Lexer/escapes.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace walnut {
namespace tokenizing {

namespace {

std::vector<ProjectName>& active() {
    static std::vector<ProjectName> names;
    return names;
}

bool name_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool valid_name(std::string_view name) {
    return !name.empty() && std::all_of(name.begin(), name.end(), name_char);
}

std::string show_code(std::uint32_t code) {
    static const char* hex = "0123456789ABCDEF";
    std::string digits;
    for (std::uint32_t v = code; v != 0 || digits.size() < 4; v >>= 4) digits.insert(digits.begin(), hex[v & 15]);
    std::string glyph;
    if (code > 0x20 && code != 0x7F && !(code >= 0x80 && code < 0xA0)) utf8_encode(code, glyph);
    return glyph.empty() ? "U+" + digits : glyph + " (U+" + digits + ")";
}

class Reader {
public:
    Reader(std::string_view text) : m_text(text) {}

    std::size_t line() const { return m_line; }
    bool done() { skip(); return m_at >= m_text.size(); }
    char peek() { skip(); return m_at < m_text.size() ? m_text[m_at] : '\0'; }
    void take() { if (m_at < m_text.size()) ++m_at; }

    std::string word() {
        skip();
        const std::size_t start = m_at;
        while (m_at < m_text.size() && (name_char(m_text[m_at]) || m_text[m_at] == '-' || m_text[m_at] == '.' || m_text[m_at] == '+')) ++m_at;
        return std::string(m_text.substr(start, m_at - start));
    }

    bool quoted(std::string& raw) {
        skip();
        if (m_at >= m_text.size() || (m_text[m_at] != '"' && m_text[m_at] != '\'')) return false;
        const char quote = m_text[m_at++];
        const std::size_t start = m_at;
        while (m_at < m_text.size() && m_text[m_at] != quote && m_text[m_at] != '\n') {
            if (m_text[m_at] == '\\' && m_at + 1 < m_text.size()) ++m_at;
            ++m_at;
        }
        raw = std::string(m_text.substr(start, m_at - start));
        if (m_at < m_text.size() && m_text[m_at] == quote) ++m_at;
        return true;
    }

    void skip_to(char stop) {
        while (m_at < m_text.size() && m_text[m_at] != stop && m_text[m_at] != '}') {
            if (m_text[m_at] == '\n') ++m_line;
            ++m_at;
        }
        if (m_at < m_text.size() && m_text[m_at] == stop) ++m_at;
    }

    void skip_block() {
        int depth = 0;
        while (!done()) {
            const char c = m_text[m_at];
            if (c == '"' || c == '\'') { std::string ignored; quoted(ignored); continue; }
            ++m_at;
            if (c == '{') ++depth;
            if (c == '}' && --depth <= 0) return;
        }
    }

private:
    void skip() {
        while (m_at < m_text.size()) {
            const char c = m_text[m_at];
            if (c == '\n') { ++m_line; ++m_at; continue; }
            if (c == ' ' || c == '\t' || c == '\r') { ++m_at; continue; }
            if (c == '/' && m_at + 1 < m_text.size() && m_text[m_at + 1] == '/') {
                while (m_at < m_text.size() && m_text[m_at] != '\n') ++m_at;
                continue;
            }
            if (c == '/' && m_at + 1 < m_text.size() && m_text[m_at + 1] == '*') {
                m_at += 2;
                while (m_at + 1 < m_text.size() && !(m_text[m_at] == '*' && m_text[m_at + 1] == '/')) {
                    if (m_text[m_at] == '\n') ++m_line;
                    ++m_at;
                }
                m_at = std::min(m_text.size(), m_at + 2);
                continue;
            }
            break;
        }
    }

    std::string_view m_text;
    std::size_t m_at = 0;
    std::size_t m_line = 1;
};

struct Pending {
    std::string name;
    std::string value;
    bool quoted = false;
    std::size_t line = 0;
};

bool parse_number(const std::string& text, std::uint32_t& code) {
    std::string digits;
    if (text.size() > 2 && (text[0] == 'U' || text[0] == 'u') && text[1] == '+') digits = text.substr(2);
    else if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) digits = text.substr(2);
    else return false;
    if (digits.empty() || digits.size() > 6) return false;
    std::uint32_t v = 0;
    for (char c : digits) {
        if (!is_hex(c)) return false;
        v = v * 16 + static_cast<std::uint32_t>(hex_val(c));
    }
    code = v;
    return true;
}

} // namespace

const char* project_file_name() {
    return "walnut.bark";
}

std::string find_project_file(const std::string& from) {
    if (from.empty()) return std::string();
    std::error_code ec;
    fs::path dir = fs::absolute(fs::path(from), ec);
    if (ec) return std::string();
    if (!fs::is_directory(dir, ec)) dir = dir.parent_path();
    while (!dir.empty()) {
        const fs::path candidate = dir / project_file_name();
        if (fs::is_regular_file(candidate, ec)) return candidate.string();
        const fs::path up = dir.parent_path();
        if (up == dir) break;
        dir = up;
    }
    return std::string();
}

ProjectNames parse_project_names(std::string_view text, const std::string& path) {
    ProjectNames out;
    out.path = path;
    Reader in(text);
    std::vector<Pending> pending;
    while (!in.done()) {
        const std::size_t at = in.line();
        const std::string section = in.word();
        if (section.empty()) {
            out.problems.push_back({ at, std::string("expected a section like 'names { ... }'") });
            in.skip_to('\n');
            continue;
        }
        if (in.peek() != '{') {
            out.problems.push_back({ at, "expected '{' after '" + section + "'" });
            in.skip_to(';');
            continue;
        }
        if (section != "names") {
            in.skip_block();
            continue;
        }
        in.take();
        while (!in.done() && in.peek() != '}') {
            Pending p;
            p.line = in.line();
            p.name = in.word();
            if (p.name.empty()) {
                out.problems.push_back({ p.line, "expected a name, as in 'deg: \"\\u{B0}\";'" });
                in.skip_to(';');
                continue;
            }
            if (in.peek() != ':') {
                out.problems.push_back({ p.line, "expected ':' after '" + p.name + "'" });
                in.skip_to(';');
                continue;
            }
            in.take();
            p.quoted = in.quoted(p.value);
            if (!p.quoted) p.value = in.word();
            if (in.peek() == ';') in.take();
            else out.problems.push_back({ p.line, "expected ';' after the value of '" + p.name + "'" });
            pending.push_back(std::move(p));
        }
        if (in.peek() == '}') in.take();
        else out.problems.push_back({ in.line(), std::string("'names' needs a closing '}'") });
    }

    std::map<std::string, const Pending*> by_name;
    for (const Pending& p : pending) by_name.emplace(p.name, &p);

    std::function<int(const Pending&, std::vector<std::string>&, std::uint32_t&, std::string&)> resolve =
        [&](const Pending& p, std::vector<std::string>& chain, std::uint32_t& code, std::string& why) -> int {
        if (p.quoted) {
            std::string decoded;
            EscapeError err = EscapeError::None;
            if (!decode_literal(p.value, decoded, err)) {
                why = "the value of '" + p.name + "' has a bad escape: " + escape_error_text(err);
                return 0;
            }
            std::size_t i = 0;
            if (decoded.empty() || !utf8_decode(decoded, i, code, err) || i != decoded.size()) {
                why = "the value of '" + p.name + "' must be exactly one character, like \"\\u{B0}\" or \"\xC2\xB0\"";
                return 0;
            }
            return 1;
        }
        if (parse_number(p.value, code)) {
            EscapeError err = EscapeError::None;
            if (valid_scalar(code, err)) return 1;
            why = "the value of '" + p.name + "' isn't a Unicode character: " + escape_error_text(err);
            return 0;
        }
        if (builtin_special_character(p.value, code)) return 1;
        const auto other = by_name.find(p.value);
        if (other != by_name.end()) {
            if (std::find(chain.begin(), chain.end(), p.value) != chain.end()) {
                why = "'" + chain.front() + "' leads back to itself (";
                for (const std::string& step : chain) why += step + " \xE2\x86\x92 ";
                why += p.value + "), so it never reaches a character";
                return 0;
            }
            chain.push_back(p.value);
            return resolve(*other->second, chain, code, why);
        }
        why = "'" + p.value + "' isn't a character name";
        const std::string guess = special_character_suggestion(p.value);
        if (!guess.empty()) why += "; did you mean '" + guess + "'?";
        else why += "; give a built-in name like alpha, a character in quotes like \"\xC2\xB0\", or a code like U+00B0";
        return 0;
    };

    std::map<std::string, std::size_t> seen;
    for (const Pending& p : pending) {
        if (!valid_name(p.name)) {
            out.problems.push_back({ p.line, "'" + p.name + "' can't be used in \\s{...}; names use only letters, digits and _" });
            continue;
        }
        std::uint32_t builtin = 0;
        if (builtin_special_character(p.name, builtin)) {
            out.problems.push_back({ p.line, "'" + p.name + "' is already a built-in name for " + show_code(builtin) + "; your names are added alongside the built-in ones, so pick another" });
            continue;
        }
        const auto again = seen.find(p.name);
        if (again != seen.end()) {
            out.problems.push_back({ p.line, "'" + p.name + "' is already given on line " + std::to_string(again->second) });
            continue;
        }
        seen[p.name] = p.line;
        std::vector<std::string> chain{ p.name };
        std::uint32_t code = 0;
        std::string why;
        if (!resolve(p, chain, code, why)) {
            out.problems.push_back({ p.line, why });
            continue;
        }
        out.names.push_back({ p.name, code, p.line });
    }
    std::sort(out.problems.begin(), out.problems.end(), [](const ProjectNameProblem& a, const ProjectNameProblem& b) { return a.line < b.line; });
    return out;
}

ProjectNames read_project_names(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        ProjectNames out;
        out.path = path;
        out.problems.push_back({ 0, std::string("can't be read") });
        return out;
    }
    std::ostringstream all;
    all << in.rdbuf();
    return parse_project_names(all.str(), path);
}

void use_project_names(const ProjectNames& names) {
    std::vector<ProjectName> sorted = names.names;
    std::sort(sorted.begin(), sorted.end(), [](const ProjectName& a, const ProjectName& b) { return a.name < b.name; });
    active() = std::move(sorted);
}

void clear_project_names() {
    active().clear();
}

const std::vector<ProjectName>& project_names() {
    return active();
}

bool special_character(std::string_view name, std::uint32_t& code) {
    if (builtin_special_character(name, code)) return true;
    const std::vector<ProjectName>& names = active();
    const auto it = std::lower_bound(names.begin(), names.end(), name, [](const ProjectName& entry, std::string_view key) { return entry.name < key; });
    if (it == names.end() || it->name != name) return false;
    code = it->code;
    return true;
}

std::vector<std::string_view> special_character_names(std::string_view prefix) {
    std::vector<std::string_view> found = builtin_special_character_names(prefix);
    for (const ProjectName& entry : active()) {
        if (std::string_view(entry.name).substr(0, prefix.size()) == prefix) found.push_back(entry.name);
    }
    std::sort(found.begin(), found.end());
    return found;
}

std::string special_character_suggestion(std::string_view name) {
    if (name.empty()) return std::string();
    std::vector<std::string_view> all = special_character_names(std::string_view());
    std::string_view best;
    std::size_t best_score = (std::max<std::size_t>(2, name.size() / 3) * 2 + 1) * 2;
    bool best_same_letters = false;
    for (const std::string_view entry : all) {
        bool same_letters = entry.size() == name.size();
        for (std::size_t k = 0; same_letters && k < name.size(); ++k) same_letters = special_names_alike(entry[k], name[k]);
        if (best_same_letters && !same_letters) continue;
        const std::size_t larger = std::max(entry.size(), name.size());
        const std::size_t smaller = std::min(entry.size(), name.size());
        if (!same_letters && (larger - smaller) * 4 >= best_score) continue;
        const std::size_t first = special_names_alike(entry[0], name[0]) ? 0 : 1;
        const std::size_t score = special_name_distance(name, entry) * 2 + first;
        if ((same_letters && !best_same_letters) || score < best_score) {
            best = entry;
            best_score = score;
            best_same_letters = same_letters;
        }
    }
    return std::string(best);
}

} // namespace tokenizing
} // namespace walnut
