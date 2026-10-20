#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Json {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0;
    std::string text;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> fields;
    std::size_t line = 0;

    const Json* get(const std::string& key) const {
        for (const auto& [k, v] : fields) {
            if (k == key) return &v;
        }
        return nullptr;
    }
};

struct ParseError {
    std::size_t line;
    std::string message;
};

void put_utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

class Parser {
public:
    explicit Parser(const std::string& text) : m_text(text) {}

    Json parse() {
        Json value = parse_value();
        skip();
        if (m_at != m_text.size()) fail("unexpected text after the end of the file's value");
        return value;
    }

private:
    [[noreturn]] void fail(const std::string& message) { throw ParseError{ m_line, message }; }

    void skip() {
        while (m_at < m_text.size()) {
            const char c = m_text[m_at];
            if (c == '\n') ++m_line;
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
            ++m_at;
        }
    }

    char peek() {
        skip();
        return m_at < m_text.size() ? m_text[m_at] : '\0';
    }

    void expect(char c) {
        if (peek() != c) fail(std::string("expected '") + c + "'" + (m_at < m_text.size() ? std::string(" but found '") + m_text[m_at] + "'" : std::string(" but the file ended")));
        ++m_at;
    }

    unsigned hex4() {
        if (m_at + 4 > m_text.size()) fail("\\u needs four hex digits");
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = m_text[m_at++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else fail("\\u needs four hex digits");
        }
        return v;
    }

    std::string parse_string() {
        expect('"');
        std::string out;
        while (true) {
            if (m_at >= m_text.size()) fail("a string never ends; add the closing '\"'");
            const char c = m_text[m_at++];
            if (c == '"') break;
            if (c == '\n') fail("a string can't go over two lines");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (m_at >= m_text.size()) fail("a string ends with a lone backslash");
            const char e = m_text[m_at++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    std::uint32_t cp = hex4();
                    if (cp >= 0xD800 && cp < 0xDC00 && m_at + 6 <= m_text.size() && m_text[m_at] == '\\' && m_text[m_at + 1] == 'u') {
                        m_at += 2;
                        const std::uint32_t low = hex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    put_utf8(out, cp);
                    break;
                }
                default: fail(std::string("unknown escape '\\") + e + "' in a string");
            }
        }
        return out;
    }

    Json parse_value() {
        Json v;
        const char c = peek();
        v.line = m_line;
        if (c == '{') {
            v.kind = Json::Kind::Object;
            ++m_at;
            if (peek() == '}') {
                ++m_at;
                return v;
            }
            while (true) {
                if (peek() != '"') fail("expected a quoted key");
                std::string key = parse_string();
                expect(':');
                v.fields.emplace_back(std::move(key), parse_value());
                if (peek() == ',') {
                    ++m_at;
                    if (peek() == '}') fail("a ',' before '}' isn't allowed in JSON; remove the last comma");
                    continue;
                }
                expect('}');
                return v;
            }
        }
        if (c == '[') {
            v.kind = Json::Kind::Array;
            ++m_at;
            if (peek() == ']') {
                ++m_at;
                return v;
            }
            while (true) {
                v.items.push_back(parse_value());
                if (peek() == ',') {
                    ++m_at;
                    if (peek() == ']') fail("a ',' before ']' isn't allowed in JSON; remove the last comma");
                    continue;
                }
                expect(']');
                return v;
            }
        }
        if (c == '"') {
            v.kind = Json::Kind::String;
            v.text = parse_string();
            return v;
        }
        if (m_text.compare(m_at, 4, "true") == 0) {
            m_at += 4;
            v.kind = Json::Kind::Bool;
            v.boolean = true;
            return v;
        }
        if (m_text.compare(m_at, 5, "false") == 0) {
            m_at += 5;
            v.kind = Json::Kind::Bool;
            return v;
        }
        if (m_text.compare(m_at, 4, "null") == 0) {
            m_at += 4;
            return v;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const std::size_t start = m_at;
            while (m_at < m_text.size() && (std::isdigit(static_cast<unsigned char>(m_text[m_at])) || std::string("+-.eE").find(m_text[m_at]) != std::string::npos)) ++m_at;
            v.kind = Json::Kind::Number;
            v.number = std::stod(m_text.substr(start, m_at - start));
            return v;
        }
        if (c == '\0') fail("the file ended where a value was expected");
        fail(std::string("unexpected '") + c + "'");
    }

    const std::string& m_text;
    std::size_t m_at = 0;
    std::size_t m_line = 1;
};

struct Character {
    std::uint32_t code = 0;
    std::vector<std::string> names;
    std::string category;
    std::size_t line = 0;
};

bool decode_one(const std::string& s, std::uint32_t& cp) {
    if (s.empty()) return false;
    const unsigned char c0 = static_cast<unsigned char>(s[0]);
    std::size_t extra = 0;
    if (c0 < 0x80) cp = c0;
    else if ((c0 & 0xE0) == 0xC0) { cp = c0 & 0x1F; extra = 1; }
    else if ((c0 & 0xF0) == 0xE0) { cp = c0 & 0x0F; extra = 2; }
    else if ((c0 & 0xF8) == 0xF0) { cp = c0 & 0x07; extra = 3; }
    else return false;
    if (s.size() != extra + 1) return false;
    for (std::size_t k = 1; k <= extra; ++k) {
        const unsigned char cn = static_cast<unsigned char>(s[k]);
        if ((cn & 0xC0) != 0x80) return false;
        cp = (cp << 6) | (cn & 0x3F);
    }
    return true;
}

bool parse_code(const std::string& text, std::uint32_t& code) {
    std::string digits;
    if (text.size() > 2 && (text[0] == 'U' || text[0] == 'u') && text[1] == '+') digits = text.substr(2);
    else if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) digits = text.substr(2);
    else return false;
    if (digits.empty() || digits.size() > 6) return false;
    std::uint32_t v = 0;
    for (const char c : digits) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= static_cast<std::uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') v |= static_cast<std::uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= static_cast<std::uint32_t>(c - 'A' + 10);
        else return false;
    }
    code = v;
    return v <= 0x10FFFF && !(v >= 0xD800 && v <= 0xDFFF);
}

std::string hex(std::uint32_t v, int width) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%0*X", width, static_cast<unsigned>(v));
    return buf;
}

std::string quoted(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

struct Table {
    std::vector<std::pair<std::string, std::uint32_t>> rows;
    std::vector<std::pair<std::uint32_t, std::string>> categories;
};

bool load(const std::string& path, Table& table) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << path << ": can't be read\n";
        return false;
    }
    std::ostringstream all;
    all << in.rdbuf();
    const std::string text = all.str();
    Json root;
    try {
        root = Parser(text).parse();
    } catch (const ParseError& e) {
        std::cerr << path << ":" << e.line << ": " << e.message << "\n";
        return false;
    }
    std::vector<std::string> problems;
    auto problem = [&](std::size_t line, const std::string& message) { problems.push_back(path + ":" + std::to_string(line) + ": " + message); };
    const Json* categories = root.kind == Json::Kind::Object ? root.get("categories") : nullptr;
    if (!categories || categories->kind != Json::Kind::Array) {
        std::cerr << path << ":1: the file needs a \"categories\" list\n";
        return false;
    }
    std::map<std::string, std::size_t> seen_names;
    std::map<std::uint32_t, std::size_t> seen_codes;
    for (const Json& cat : categories->items) {
        const Json* name = cat.get("name");
        const Json* chars = cat.get("characters");
        if (cat.kind != Json::Kind::Object || !name || name->kind != Json::Kind::String || !chars || chars->kind != Json::Kind::Array) {
            problem(cat.line, "each category needs a \"name\" and a \"characters\" list");
            continue;
        }
        for (const Json& ch : chars->items) {
            const Json* code_value = ch.get("code");
            const Json* names = ch.get("names");
            const Json* symbol = ch.get("symbol");
            std::uint32_t code = 0;
            if (ch.kind != Json::Kind::Object || !code_value || code_value->kind != Json::Kind::String || !parse_code(code_value->text, code)) {
                problem(ch.line, "each character needs a \"code\" like \"U+03B1\"");
                continue;
            }
            const auto again = seen_codes.find(code);
            if (again != seen_codes.end()) problem(ch.line, "U+" + hex(code, 4) + " is already listed on line " + std::to_string(again->second) + "; give it all its names in one place");
            else seen_codes[code] = ch.line;
            if (symbol && symbol->kind == Json::Kind::String) {
                std::uint32_t written = 0;
                if (!decode_one(symbol->text, written)) problem(ch.line, "\"symbol\" must be exactly one character");
                else if (written != code) problem(ch.line, "\"symbol\" is U+" + hex(written, 4) + " but \"code\" says U+" + hex(code, 4) + "; fix one so they match");
            }
            if (!names || names->kind != Json::Kind::Array) {
                problem(ch.line, "each character needs a \"names\" list (it can be empty)");
                continue;
            }
            table.categories.emplace_back(code, name->text);
            for (const Json& n : names->items) {
                if (n.kind != Json::Kind::String || n.text.empty()) {
                    problem(ch.line, "names must be non-empty strings");
                    continue;
                }
                if (!std::all_of(n.text.begin(), n.text.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; })) {
                    problem(ch.line, "'" + n.text + "' can't be used in \\s{...}; names use only letters, digits and _");
                    continue;
                }
                const auto taken = seen_names.find(n.text);
                if (taken != seen_names.end()) {
                    problem(ch.line, "'" + n.text + "' is already a name on line " + std::to_string(taken->second));
                    continue;
                }
                seen_names[n.text] = ch.line;
                table.rows.emplace_back(n.text, code);
            }
        }
    }
    if (!problems.empty()) {
        for (const std::string& p : problems) std::cerr << p << "\n";
        std::cerr << path << ": " << problems.size() << (problems.size() == 1 ? " problem" : " problems") << "; the special names weren't generated\n";
        return false;
    }
    std::sort(table.rows.begin(), table.rows.end());
    std::sort(table.categories.begin(), table.categories.end());
    return true;
}

std::string table_rows(const Table& t) {
    std::string out;
    for (const auto& [name, code] : t.rows) out += "    { " + quoted(name) + ", 0x" + hex(code, 1) + " },\n";
    return out;
}

const char* k_lookup = R"CPP(
std::size_t letter_cost(char x, char y) {
    if (x == y) return 0;
    return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)) ? 1 : 2;
}

std::size_t edit_distance(std::string_view a, std::string_view b) {
    std::vector<std::size_t> before(b.size() + 1), above(b.size() + 1), row(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) above[j] = j * 2;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        row[0] = i * 2;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            row[j] = std::min({ above[j] + 2, row[j - 1] + 2, above[j - 1] + letter_cost(a[i - 1], b[j - 1]) });
            if (i > 1 && j > 1 && letter_cost(a[i - 1], b[j - 2]) == 0 && letter_cost(a[i - 2], b[j - 1]) == 0) row[j] = std::min(row[j], before[j - 2] + 2);
        }
        before.swap(above);
        above.swap(row);
    }
    return above[b.size()];
}

} // namespace

std::size_t special_name_distance(std::string_view a, std::string_view b) {
    return edit_distance(a, b);
}

bool special_names_alike(char a, char b) {
    return letter_cost(a, b) < 2;
}

bool builtin_special_character(std::string_view name, std::uint32_t& code) {
    const auto* first = std::begin(k_special_names);
    const auto* last = std::end(k_special_names);
    const auto* it = std::lower_bound(first, last, name, [](const SpecialName& entry, std::string_view key) { return entry.name < key; });
    if (it == last || it->name != name) return false;
    code = it->code;
    return true;
}

std::vector<std::string_view> builtin_special_character_names(std::string_view prefix) {
    std::vector<std::string_view> found;
    for (const SpecialName& entry : k_special_names) {
        if (entry.name.substr(0, prefix.size()) == prefix) found.push_back(entry.name);
    }
    return found;
}
)CPP";

std::string source_file(const Table& t, const std::string& include, const std::string& open, const std::string& close, bool categories) {
    std::string out = include + "\n\n#include <algorithm>\n#include <cctype>\n#include <cstddef>\n#include <cstdint>\n#include <iterator>\n#include <string>\n#include <string_view>\n#include <vector>\n\n";
    out += open + "\n\nnamespace {\n\nstruct SpecialName {\n    std::string_view name;\n    std::uint32_t code;\n};\n\nconstexpr SpecialName k_special_names[] = {\n";
    out += table_rows(t);
    out += "};\n";
    if (categories) {
        out += "\nstruct SpecialCategory {\n    std::uint32_t code;\n    const char* name;\n};\n\nconstexpr SpecialCategory k_special_categories[] = {\n";
        for (const auto& [code, name] : t.categories) out += "    { 0x" + hex(code, 1) + ", " + quoted(name) + " },\n";
        out += "};\n";
    }
    out += k_lookup;
    if (categories) {
        out += "\nconst char* builtin_special_category(std::uint32_t code) {\n"
               "    const auto* first = std::begin(k_special_categories);\n"
               "    const auto* last = std::end(k_special_categories);\n"
               "    const auto* it = std::lower_bound(first, last, code, [](const SpecialCategory& entry, std::uint32_t key) { return entry.code < key; });\n"
               "    return it != last && it->code == code ? it->name : nullptr;\n"
               "}\n";
    }
    out += "\n" + close + "\n";
    return out;
}

std::string runtime_file(const Table& t) {
    std::string out = "#ifndef WALNUT_SPECIAL_NAMES_HPP\n#define WALNUT_SPECIAL_NAMES_HPP\n\n#include <algorithm>\n#include <cstdint>\n#include <iterator>\n#include <string_view>\n\nnamespace walnut_rt {\n\n"
                      "struct special_name_entry {\n    std::string_view name;\n    std::uint32_t code;\n};\n\ninline constexpr special_name_entry special_names[] = {\n";
    out += table_rows(t);
    out += "};\n\ninline bool special_character(std::string_view name, std::uint32_t& code) {\n"
           "    const auto* first = std::begin(special_names);\n"
           "    const auto* last = std::end(special_names);\n"
           "    const auto* it = std::lower_bound(first, last, name, [](const special_name_entry& entry, std::string_view key) { return entry.name < key; });\n"
           "    if (it == last || it->name != name) return false;\n"
           "    code = it->code;\n"
           "    return true;\n"
           "}\n\n} // namespace walnut_rt\n\n#endif // WALNUT_SPECIAL_NAMES_HPP\n";
    return out;
}

bool write_if_changed(const std::string& path, const std::string& text) {
    {
        std::ifstream in(path, std::ios::binary);
        if (in) {
            std::ostringstream old;
            old << in.rdbuf();
            if (old.str() == text) {
                in.close();
                std::error_code ec;
                std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), ec);
                return true;
            }
        }
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << path << ": can't be written\n";
        return false;
    }
    out << text;
    return static_cast<bool>(out);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: special_names_gen lexer|runtime|canopy <special_names.json> <output>\n";
        return 2;
    }
    const std::string mode = argv[1];
    Table table;
    if (!load(argv[2], table)) return 1;
    std::string text;
    if (mode == "lexer") text = source_file(table, "#include \"Lexer/special_names.hpp\"", "namespace walnut {\nnamespace tokenizing {", "} // namespace tokenizing\n} // namespace walnut", false);
    else if (mode == "canopy") text = source_file(table, "#include <canopy/special_names.hpp>", "namespace canopy {", "} // namespace canopy", true);
    else if (mode == "runtime") text = runtime_file(table);
    else {
        std::cerr << "special_names_gen: unknown mode '" << mode << "'; use lexer, runtime or canopy\n";
        return 2;
    }
    return write_if_changed(argv[3], text) ? 0 : 1;
}
