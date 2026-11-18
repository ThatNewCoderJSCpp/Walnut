#include "walnut_cli.hpp"

namespace walnut_cli {

int highlight(const Options& o) {
    std::string text;

    if (o.from_stdin) {
        std::ostringstream in;
        in << std::cin.rdbuf();
        text = in.str();
    }

    walnut::CompilerPipeline compiler(o.input);
    compiler.set_path_style(walnut::PathStyle::Absolute);
    compiler.report_silent();
    compiler.warn_silent();
    if (o.ownership) compiler.enable_ownership_checks();
    if (o.warn_unused) compiler.enable_unused_warnings();
    if (o.overflow_errors) compiler.enable_overflow_errors();
    for (const auto& d : o.include_dirs) compiler.add_include_dir(d);
    for (const auto& d : o.module_dirs) compiler.add_module_dir(d);
    for (const auto& d : o.module_env_dirs) compiler.add_module_dir(d);
    compiler.add_module_dir(o.runtime_dir);
    for (const walnut_library::File& f : walnut_library::files) if (f.path) compiler.add_library_file(f.path, std::string(reinterpret_cast<const char*>(f.data), f.size));
    for (const auto& [name, value] : o.defines) compiler.define_macro(name, value);
    for (const std::string& extra : o.extra_inputs) compiler.add_input(extra);
    compiler.set_native_header_args([&o](bool cpp) { return native_header_args(o, cpp); });
    if (o.from_stdin) compiler.set_input_text(std::move(text));

    try {
        compiler.compile();
    } catch (const std::exception&) {
    }

    std::cout << walnut::driver::highlight_json(compiler);
    std::cout.flush();
    return 0;
}

bool completion_ident(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) || c == '_' || u >= 0x80;
}

std::string completion_pass(const Options& o, std::string text, std::string_view placeholder, const walnut::driver::CompletionRequest& request) {
    walnut::CompilerPipeline compiler(o.input);
    compiler.set_path_style(walnut::PathStyle::Absolute);
    compiler.report_silent();
    compiler.warn_silent();
    for (const auto& d : o.include_dirs) compiler.add_include_dir(d);
    for (const auto& d : o.module_dirs) compiler.add_module_dir(d);
    for (const auto& d : o.module_env_dirs) compiler.add_module_dir(d);
    compiler.add_module_dir(o.runtime_dir);
    for (const walnut_library::File& f : walnut_library::files) if (f.path) compiler.add_library_file(f.path, std::string(reinterpret_cast<const char*>(f.data), f.size));
    for (const auto& [name, value] : o.defines) compiler.define_macro(name, value);
    for (const std::string& extra : o.extra_inputs) compiler.add_input(extra);
    compiler.set_native_header_args([&o](bool cpp) { return native_header_args(o, cpp); });
    compiler.set_input_text(std::move(text));
    try {
        compiler.compile();
    } catch (const std::exception&) {
    }
    if (compiler.had_front_end_errors()) return std::string();
    return walnut::driver::complete_json(compiler, placeholder, request);
}

int complete(const Options& o) {
    std::string text;
    if (o.from_stdin) {
        std::ostringstream in;
        in << std::cin.rdbuf();
        text = in.str();
    } else {
        std::ifstream in(o.input, std::ios::binary);
        if (!in) { std::cerr << "walnut: cannot read '" << o.input << "'\n"; return 1; }
        std::ostringstream all;
        all << in.rdbuf();
        text = all.str();
    }
    if (o.at_line == 0) { std::cerr << "walnut: complete needs --at LINE:COLUMN\n"; return 2; }

    std::size_t line_start = 0;
    for (std::size_t l = 1; l < o.at_line && line_start <= text.size(); ++l) {
        const std::size_t nl = text.find('\n', line_start);
        if (nl == std::string::npos) { line_start = text.size() + 1; break; }
        line_start = nl + 1;
    }
    if (line_start > text.size()) line_start = text.size();
    std::size_t line_end = text.find('\n', line_start);
    if (line_end == std::string::npos) line_end = text.size();
    const std::size_t caret = std::min(line_start + o.at_column - 1, line_end);
    std::size_t start = caret;
    while (start > line_start && completion_ident(text[start - 1])) --start;
    std::size_t end = caret;
    while (end < line_end && completion_ident(text[end])) ++end;

    walnut::driver::CompletionRequest request;
    request.start = start - line_start;
    request.line = static_cast<std::uint32_t>(o.at_line);
    std::size_t before = start;
    auto ends_with = [&](std::string_view s) { return before >= line_start + s.size() && std::string_view(text).substr(before - s.size(), s.size()) == s; };
    if (ends_with("::")) request.mode = walnut::driver::CompletionRequest::Mode::Scope;
    else if (ends_with("->")) request.mode = walnut::driver::CompletionRequest::Mode::Arrow;
    else if (ends_with(".") && !ends_with("..")) request.mode = walnut::driver::CompletionRequest::Mode::Member;
    if (request.mode != walnut::driver::CompletionRequest::Mode::Name) {
        std::size_t q = before - (request.mode == walnut::driver::CompletionRequest::Mode::Member ? 1 : 2);
        std::vector<std::string> parts;
        while (q > line_start) {
            std::size_t w = q;
            while (w > line_start && completion_ident(text[w - 1])) --w;
            if (w == q) break;
            parts.insert(parts.begin(), text.substr(w, q - w));
            if (request.mode != walnut::driver::CompletionRequest::Mode::Scope || w < line_start + 2 || text.compare(w - 2, 2, "::") != 0) break;
            q = w - 2;
        }
        request.qualifier = std::move(parts);
    }

    const std::string placeholder = "__walnut_complete__";
    bool rest_blank = true;
    for (std::size_t i = end; i < line_end; ++i) rest_blank = rest_blank && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r');
    std::string patched = text.substr(0, start) + placeholder + (rest_blank ? ";" : "") + text.substr(end);
    std::string json = completion_pass(o, patched, placeholder, request);
    if (json.empty() && !rest_blank) {
        patched = text.substr(0, start) + placeholder + ";" + text.substr(line_end);
        json = completion_pass(o, patched, placeholder, request);
    }
    if (json.empty()) {
        std::string blank = text;
        for (std::size_t i = line_start; i < line_end; ++i) if (blank[i] != '\r') blank[i] = ' ';
        json = completion_pass(o, blank, placeholder, request);
    }
    if (json.empty()) json = "{\"start\":" + std::to_string(request.start) + ",\"mode\":\"none\",\"items\":[]}\n";
    std::cout << json;
    std::cout.flush();
    return 0;
}

bool load_project_names(const Options& o) {
    namespace wt = walnut::tokenizing;
    wt::clear_project_names();
    const std::string file = wt::find_project_file(o.input);
    if (file.empty()) return true;
    const wt::ProjectNames names = wt::read_project_names(file);
    wt::use_project_names(names);
    if (names.problems.empty() || o.command == Command::Highlight || o.command == Command::Complete) return true;
    std::error_code ec;
    std::string shown = fs::relative(file, fs::current_path(ec), ec).generic_string();
    if (ec || shown.empty() || shown.rfind("..", 0) == 0) shown = file;
    for (const wt::ProjectNameProblem& p : names.problems) {
        std::cerr << "[Project] " << shown;
        if (p.line > 0) std::cerr << ":" << p.line;
        std::cerr << ": " << p.message << "\n";
    }
    std::cerr << "walnut: " << shown << " has " << names.problems.size() << (names.problems.size() == 1 ? " problem" : " problems") << " in its 'names' section\n";
    return false;
}

std::string demangled(const std::string& symbol) {
    std::string name = symbol;
    if (name.rfind("__Z", 0) == 0) name.erase(0, 1);
#if defined(WALNUT_HAS_DEMANGLE)
    if (name.rfind("_Z", 0) == 0) {
        int status = 0;
        char* out = abi::__cxa_demangle(name.c_str(), nullptr, nullptr, &status);
        if (out && status == 0) {
            std::string result = out;
            std::free(out);
            return result;
        }
        std::free(out);
    }
#endif
    return symbol;
}

std::string without_parameters(const std::string& name) {
    int depth = 0;
    for (std::size_t i = 0; i < name.size(); ++i) {
        if (name[i] == '<') ++depth;
        else if (name[i] == '>') --depth;
        else if (name[i] == '(' && depth == 0 && i > 0) return name.substr(0, i);
    }
    return name;
}

std::string sanitize_name(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (std::isalnum(c)) out += static_cast<char>(c);
        else if (c == ':' && i + 1 < s.size() && s[i + 1] == ':') { out += "_N"; ++i; }
        else if (c == '_') out += "_u";
        else if (c == ' ') continue;
        else { out += '_'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

std::string decode_walnut_name(const std::string& text, std::size_t& i) {
    auto hex = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) || (c >= 'A' && c <= 'F'); };
    std::string out;

    while (i < text.size()) {
        const char c = text[i];
        if (std::isalnum(static_cast<unsigned char>(c))) { out += c; ++i; continue; }
        if (c != '_') break;
        if (i + 1 < text.size() && text[i + 1] == 'u') { out += '_'; i += 2; continue; }
        if (i + 1 < text.size() && text[i + 1] == 'N') { out += "::"; i += 2; continue; }
        if (i + 2 < text.size() && hex(text[i + 1]) && hex(text[i + 2])) {
            out += static_cast<char>(std::stoi(text.substr(i + 1, 2), nullptr, 16));
            i += 3;
            continue;
        }
        break;
    }

    return out;
}

std::string readable_name(const std::string& symbol) {
    std::string name = without_parameters(demangled(symbol));
    if (name.rfind("wlu::", 0) != 0) return name;
    name = name.substr(5);
    std::size_t i = 2;
    const bool instance = name.find("_I_") != std::string::npos;

    if (name.rfind("f_", 0) == 0) return decode_walnut_name(name, i) + (instance ? " (template instance)" : "");

    if (name.rfind("R_", 0) == 0) {
        const std::string record = decode_walnut_name(name, i);
        const std::size_t sep = name.find("::");
        if (sep == std::string::npos) return record;
        const std::string rest = name.substr(sep + 2);
        if (rest.rfind("m_", 0) == 0) {
            std::size_t j = 2;
            return record + "::" + decode_walnut_name(rest, j) + (instance ? " (template instance)" : "");
        }
        const std::size_t colon = record.rfind("::");
        const std::string last = colon == std::string::npos ? record : record.substr(colon + 2);
        if (rest.rfind("~", 0) == 0) return record + "::~" + last;
        if (rest.rfind("R_", 0) == 0) return record + "::" + last;
        return record + "::" + rest;
    }

    return "wlu::" + name;
}

bool function_matches(const std::string& query, const std::string& symbol) {
    const std::string full = without_parameters(demangled(symbol));
    if (full == query || symbol == query || symbol == "_" + query) return true;
    std::string name = full;
    if (name.rfind("wlu::", 0) == 0) name = name.substr(5);
    const std::size_t split = query.rfind("::");
    const std::string whole = sanitize_name(query);

    if (name.rfind("f_" + whole + "_", 0) == 0) return true;
    if (name.rfind("f_", 0) == 0 && name.find("_N" + whole + "_") != std::string::npos) return true;

    if (split != std::string::npos) {
        const std::string owner = sanitize_name(query.substr(0, split));
        const std::string member = query.substr(split + 2);
        const std::size_t sep = name.find("::");
        if (sep != std::string::npos && name.rfind("R_", 0) == 0) {
            const std::string record = name.substr(0, sep);
            const std::string rest = name.substr(sep + 2);
            const bool owner_ok = record.rfind("R_" + owner, 0) == 0 || record.find("_N" + owner) != std::string::npos;
            const std::size_t last = query.rfind("::", split - 1);
            const std::string owner_last = query.substr(last == std::string::npos || split == 0 ? 0 : last + 2, split - (last == std::string::npos || split == 0 ? 0 : last + 2));
            const bool special = (rest == record && member == owner_last) || (rest == "~" + record && member == "~" + owner_last);
            const bool member_ok = rest.rfind("m_" + sanitize_name(member) + "_", 0) == 0 || special;
            if (owner_ok && member_ok) return true;
        }
    }

    return false;
}

std::vector<DumpBlock> assembly_functions(const std::string& text) {
    std::vector<std::string> lines;
    {
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
    }

    auto trim = [](const std::string& x) {
        std::size_t a = 0;
        while (a < x.size() && (x[a] == ' ' || x[a] == '\t')) ++a;
        return x.substr(a);
    };

    auto label_of = [](const std::string& line) -> std::string {
        if (line.empty() || line[0] == ' ' || line[0] == '\t' || line[0] == '#' || line[0] == ';') return std::string();
        std::size_t end = 0;
        if (line[0] == '"') {
            end = line.find('"', 1);
            if (end == std::string::npos || end + 1 >= line.size() || line[end + 1] != ':') return std::string();
            return line.substr(1, end - 1);
        }
        while (end < line.size() && line[end] != ':' && line[end] != ' ' && line[end] != '\t') ++end;
        if (end == 0 || end >= line.size() || line[end] != ':') return std::string();
        return line.substr(0, end);
    };

    std::vector<std::string> typed;
    for (const std::string& raw : lines) {
        const std::string l = trim(raw);
        if (l.rfind(".type", 0) == 0 && (l.find("@function") != std::string::npos || l.find("%function") != std::string::npos)) {
            std::string sym = trim(l.substr(5));
            sym = sym.substr(0, sym.find(','));
            if (!sym.empty() && sym.front() == '"') sym = sym.substr(1, sym.size() - 2);
            typed.push_back(sym);
        } else if (l.rfind(".def", 0) == 0) {
            const std::size_t type = l.find(".type");
            if (type == std::string::npos || trim(l.substr(type + 5)).rfind("32", 0) != 0) continue;
            std::string sym = trim(l.substr(4));
            sym = sym.substr(0, sym.find(';'));
            typed.push_back(sym);
        }
    }

    auto is_function = [&](std::size_t i, const std::string& sym) {
        if (sym.rfind(".L", 0) == 0 || (sym.size() > 1 && sym[0] == 'L' && (sym.rfind("Ltmp", 0) == 0 || sym.rfind("LBB", 0) == 0 || sym.rfind("Lfunc", 0) == 0))) return false;
        if (std::find(typed.begin(), typed.end(), sym) != typed.end()) return true;
        for (std::size_t j = i + 1; j < lines.size() && j <= i + 4; ++j) if (trim(lines[j]).rfind(".cfi_startproc", 0) == 0) return true;
        return false;
    };

    std::vector<DumpBlock> out;

    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string sym = label_of(lines[i]);
        if (sym.empty() || !is_function(i, sym)) continue;
        DumpBlock b;
        b.symbol = sym;
        std::size_t j = i;

        for (; j < lines.size(); ++j) {
            if (j > i) {
                const std::string other = label_of(lines[j]);
                if (!other.empty() && is_function(j, other)) break;
            }
            b.lines.push_back(lines[j]);
            const std::string l = trim(lines[j]);
            if (l.rfind(".size", 0) == 0 && l.find(sym) != std::string::npos) { ++j; break; }
            if (l.rfind(".cfi_endproc", 0) == 0) {
                for (std::size_t k = j + 1; k < lines.size() && k <= j + 3; ++k) {
                    const std::string n = trim(lines[k]);
                    if (n.rfind(".size", 0) == 0 && n.find(sym) != std::string::npos) { b.lines.push_back(lines[k]); j = k; break; }
                    if (!label_of(lines[k]).empty()) break;
                }
                ++j;
                break;
            }
        }

        out.push_back(std::move(b));
        i = j - 1;
    }

    return out;
}

std::vector<DumpBlock> llvm_functions(const std::string& text) {
    std::vector<DumpBlock> out;
    std::istringstream in(text);
    std::string line;
    DumpBlock* open = nullptr;

    while (std::getline(in, line)) {
        if (!open && line.rfind("define ", 0) == 0) {
            const std::size_t at = line.find('@');
            if (at == std::string::npos) continue;
            std::size_t end = at + 1;
            std::string sym;
            if (end < line.size() && line[end] == '"') {
                const std::size_t close = line.find('"', end + 1);
                sym = line.substr(end + 1, close - end - 1);
            } else {
                while (end < line.size() && line[end] != '(') ++end;
                sym = line.substr(at + 1, end - at - 1);
            }
            out.push_back(DumpBlock{ sym, {} });
            open = &out.back();
        }

        if (open) {
            open->lines.push_back(line);
            if (line == "}") open = nullptr;
        }
    }

    return out;
}

std::string filter_dump(const Options& o, const std::string& text, bool& ok) {
    ok = true;
    if (o.asm_functions.empty()) return text;
    const std::vector<DumpBlock> blocks = o.dump == Dump::LlvmIr ? llvm_functions(text) : assembly_functions(text);
    const std::string comment = o.dump == Dump::LlvmIr ? "; " : (target_triple(o).rfind("aarch64", 0) == 0 || target_triple(o).rfind("arm", 0) == 0 ? "// " : "# ");
    std::string out;

    for (const std::string& q : o.asm_functions) {
        std::vector<const DumpBlock*> found;
        for (const DumpBlock& b : blocks) if (function_matches(q, b.symbol)) found.push_back(&b);

        if (found.empty()) {
            for (const DumpBlock& b : blocks) {
                if (demangled(b.symbol).find(q) != std::string::npos) found.push_back(&b);
            }
        }

        if (found.empty()) {
            std::cerr << "walnut: no function named '" << q << "' in the " << (o.dump == Dump::LlvmIr ? "LLVM IR" : "assembly");
            std::vector<std::string> names;
            for (const DumpBlock& b : blocks) {
                const std::string raw = without_parameters(demangled(b.symbol));
                if (raw.rfind("wlu::f_", 0) != 0 && raw.rfind("wlu::R_", 0) != 0) continue;
                const std::string n = readable_name(b.symbol);
                if (n.rfind("std::", 0) == 0) continue;
                if (std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n);
            }
            if (!names.empty()) {
                std::cerr << "; the program's functions are:\n";
                for (std::size_t i = 0; i < names.size() && i < 40; ++i) std::cerr << "  " << names[i] << "\n";
                if (names.size() > 40) std::cerr << "  ... and " << (names.size() - 40) << " more\n";
            } else {
                std::cerr << "\n";
            }
            std::cerr << "walnut: it may have been inlined everywhere it's used; -O0 keeps every function\n";
            ok = false;
            continue;
        }

        for (const DumpBlock* b : found) {
            out += comment + readable_name(b->symbol) + "\n";
            out += comment + demangled(b->symbol) + "\n";
            for (const std::string& l : b->lines) out += l + "\n";
            out += "\n";
        }
    }

    return out;
}

std::string display_path(const std::string& path) {
    if (is_library_path(path)) return path;
    std::error_code ec;
    const fs::path rel = fs::relative(path, fs::current_path(ec), ec);
    if (!ec && !rel.empty() && rel.generic_string().rfind("..", 0) != 0) return rel.generic_string();
    return fs::path(path).generic_string();
}

class WalnutSourcePrinter {
public:
    explicit WalnutSourcePrinter(const walnut::SourceManager& sources) : m_sources(sources) {}

    std::string print(const walnut::CompilerPipeline::PreprocessedFile& f) {
        m_out.clear();
        m_stack.clear();
        m_prev = nullptr;
        m_line_start = true;
        std::size_t mark = 0;
        m_out += "// " + display_path(m_sources.absolute_path(f.file)) + "\n";

        for (std::size_t i = 0; i <= f.tokens.size(); ++i) {
            while (mark < f.marks.size() && f.marks[mark].at == i) {
                const walnut::preprocessing::FileMark& m = f.marks[mark++];
                if (m.enter) enter(m.file);
                else leave();
            }

            if (i == f.tokens.size()) break;
            const walnut::tokenizing::Token& t = f.tokens[i];
            if (t.is(walnut::tokenizing::Token::Kind::End)) continue;
            put(t);
        }

        if (!m_out.empty() && m_out.back() != '\n') m_out += '\n';
        return m_out;
    }

private:
    using Token = walnut::tokenizing::Token;
    using Kind = Token::Kind;

    struct Region {
        walnut::FileId file = walnut::INVALID_FILE;
        std::uint32_t  line = 0;
        std::string    indent;
    };

    const walnut::SourceManager&                           m_sources;
    std::string                                            m_out;
    std::vector<Region>                                    m_stack;
    std::map<walnut::FileId, std::vector<std::size_t>>     m_line_starts;
    bool                                                   m_line_start = true;
    const Token*                                           m_prev = nullptr;
    bool                                                   m_prev_original = false;

    void newline() {
        if (!m_out.empty() && m_out.back() != '\n') m_out += '\n';
        m_line_start = true;
        m_prev = nullptr;
    }

    void enter(walnut::FileId file) {
        if (!m_stack.empty()) {
            newline();
            m_out += m_stack.back().indent + "// begin " + display_path(m_sources.absolute_path(file)) + "\n";
        }
        m_stack.push_back(Region{ file, 0, std::string() });
        m_line_start = true;
        m_prev = nullptr;
    }

    void leave() {
        if (m_stack.empty()) return;
        const walnut::FileId file = m_stack.back().file;
        m_stack.pop_back();
        if (m_stack.empty()) return;
        newline();
        m_out += m_stack.back().indent + "// end " + display_path(m_sources.absolute_path(file)) + "\n";
        m_line_start = true;
        m_prev = nullptr;
    }

    const std::vector<std::size_t>& line_starts(walnut::FileId file) {
        auto it = m_line_starts.find(file);
        if (it != m_line_starts.end()) return it->second;
        std::vector<std::size_t> starts{ 0 };
        const char* data = m_sources.data(file);
        const std::size_t size = m_sources.size(file);
        for (std::size_t i = 0; i < size; ++i) if (data[i] == '\n') starts.push_back(i + 1);
        return m_line_starts.emplace(file, std::move(starts)).first->second;
    }

    bool in_buffer(const Token& t, walnut::FileId file) const {
        if (file == walnut::INVALID_FILE || file >= m_sources.file_count()) return false;
        const char* data = m_sources.data(file);
        return t.data() >= data && t.data() + t.length() <= data + m_sources.size(file);
    }

    std::uint32_t line_at(walnut::FileId file, const char* p) {
        const std::vector<std::size_t>& starts = line_starts(file);
        const std::size_t offset = static_cast<std::size_t>(p - m_sources.data(file));
        return static_cast<std::uint32_t>(std::upper_bound(starts.begin(), starts.end(), offset) - starts.begin());
    }

    bool original(const Token& t, walnut::FileId region) {
        if (t.file_id() != region || !in_buffer(t, region)) return false;
        return line_at(region, t.data()) == t.line() || line_at(region, t.data() + t.length()) == t.line();
    }

    std::string indentation(walnut::FileId file, const char* p) {
        const std::vector<std::size_t>& starts = line_starts(file);
        const char* data = m_sources.data(file);
        const std::size_t offset = static_cast<std::size_t>(p - data);
        const std::size_t start = *(std::upper_bound(starts.begin(), starts.end(), offset) - 1);
        std::string out;
        for (std::size_t i = start; i < offset && (data[i] == ' ' || data[i] == '\t'); ++i) out += data[i];
        return out;
    }

    static std::string spelling(const Token& t) {
        const std::string text(t.lexeme());
        if (t.is(Kind::String)) return "\"" + text + "\"";
        if (t.is(Kind::Character)) return "'" + text + "'";
        if (t.is(Kind::TextLiteral)) return "`" + text + "`";
        if (t.is(Kind::RawCode)) return "code {" + text + "}";
        return text;
    }

    static bool word_like(const std::string& s) {
        return !s.empty() && (std::isalnum(static_cast<unsigned char>(s.back())) || s.back() == '_' || s.back() == '"' || s.back() == '\'' || s.back() == '`');
    }

    static bool needs_space(const Token& prev, const Token& cur) {
        const std::string a = spelling(prev);
        const std::string b = spelling(cur);
        for (const char* tight : { "(", "[", ".", "::", "->", "~", "?.", "@" }) if (a == tight) return false;
        for (const char* tight : { ")", "]", ",", ";", ".", "::", "->", "?.", "...", "++", "--" }) if (b == tight) return false;
        if ((b == "(" || b == "[") && (prev.is(Kind::Identifier) || a == ")" || a == "]" || a == ">")) return false;
        if ((a == "++" || a == "--" || a == "!") && word_like(b)) return false;
        return true;
    }

    void put(const Token& t) {
        if (m_stack.empty()) return;
        Region& region = m_stack.back();
        const bool orig = original(t, region.file);

        if (t.line() > region.line) {
            const bool first = region.line == 0;
            const std::uint32_t gap = first ? 0 : t.line() - region.line;
            newline();
            if (gap > 1) m_out += '\n';
            region.line = t.line();
        }

        if (m_line_start) {
            region.indent = orig ? indentation(region.file, t.data()) : region.indent;
            m_out += region.indent;
        } else if (m_prev) {
            bool done = false;
            if (orig && m_prev_original && m_prev->data() + m_prev->length() <= t.data() && in_buffer(*m_prev, region.file)) {
                const char* from = m_prev->data() + m_prev->length();
                const char* to = t.data();
                if (t.is(Kind::String) || t.is(Kind::Character) || t.is(Kind::TextLiteral)) --to;
                if (m_prev->is(Kind::String) || m_prev->is(Kind::Character) || m_prev->is(Kind::TextLiteral)) ++from;
                if (from <= to) {
                    std::string gap(from, to);
                    if (gap.find_first_not_of(" \t") == std::string::npos) { m_out += gap; done = true; }
                }
            }
            if (!done && needs_space(*m_prev, t)) m_out += ' ';
        }

        m_out += spelling(t);
        m_line_start = false;
        m_prev = &t;
        m_prev_original = orig;
    }
};

int dump_walnut_preprocessed(const Options& o) {
    walnut::CompilerPipeline compiler(o.input);
    compiler.set_path_style(fs::path(o.input).is_absolute() ? walnut::PathStyle::Absolute : walnut::PathStyle::Relative);
    configure_pipeline(compiler, o);
    if (o.no_warnings) compiler.suppress_warnings();
    for (const std::string& extra : o.extra_inputs) compiler.add_input(extra);
    if (!route_messages(compiler.error_reporter(), o.errors_to, "--errors")) return 1;
    if (!route_messages(compiler.warning_reporter(), o.warnings_to, "--warnings")) return 1;
    std::vector<walnut::CompilerPipeline::PreprocessedFile> files;

    try {
        files = compiler.preprocess_only();
    } catch (const std::exception& e) {
        std::cerr << "walnut: " << e.what() << "\n";
        return 1;
    }

    if (compiler.error_reporter().has_errors() || files.empty()) return 1;
    WalnutSourcePrinter printer(compiler.get_sources());
    std::string text;

    for (std::size_t i = 0; i < files.size(); ++i) {
        if (i) text += "\n";
        text += printer.print(files[i]);
    }

    fs::path out;
    if (!o.output.empty()) out = o.output;
    else if (o.command == Command::Build) out = fs::path(o.input).filename().replace_extension(".pp.wal");
    if (out.empty()) { std::cout << text; return 0; }
    return write_file(out, text) ? 0 : 1;
}

int dump_program(const Options& o) {
    std::string raw;
    if (!generate(o, raw, nullptr, o.prebuilt_std)) return 1;

    if (!fs::exists(fs::path(o.runtime_dir) / "walnut_runtime.hpp")) {
        std::cerr << "walnut: runtime header not found in '" << o.runtime_dir << "' (use --runtime or WALNUT_RUNTIME_DIR)\n";
        return 1;
    }

    if (!find_program(o.cxx)) {
        explain_missing_compiler(o.cxx);
        return 1;
    }

    if (o.dump == Dump::LlvmIr && compiler_query(o, "--version").find("clang") == std::string::npos) {
        std::cerr << "walnut: --llvm-ir needs clang as the C++ compiler (" << o.cxx << " isn't clang); pass --cxx clang++\n";
        return 1;
    }

    if (o.asm_syntax == "intel") {
        const std::string t = target_triple(o);
        if (!t.empty() && t.rfind("x86_64", 0) != 0 && t.rfind("i386", 0) != 0 && t.rfind("i486", 0) != 0 && t.rfind("i586", 0) != 0 && t.rfind("i686", 0) != 0 && t.rfind("amd64", 0) != 0) {
            std::cerr << "walnut: --asm-syntax only applies to x86, and this compiler makes code for " << t << "\n";
            return 1;
        }
    }

    const fs::path tmp = make_temp_dir();
    const fs::path cpp_path = tmp / "program.cpp";
    std::vector<LineMapEntry> map;
    if (!write_file(cpp_path, finalize_cpp(raw, o, cpp_path.string(), map))) return 1;
    const char* ext = o.dump == Dump::Assembly ? ".s" : (o.dump == Dump::LlvmIr ? ".ll" : ".ii");
    const fs::path out_tmp = tmp / (std::string("program") + ext);
    std::string cmd = cxx_cmd(o) + " " + compile_flags(o);

    if (o.dump == Dump::Assembly) {
        cmd += " -S";
        if (o.asm_syntax == "intel") cmd += " -masm=intel";
        else if (o.asm_syntax == "att") cmd += " -masm=att";
        if (o.verbose_asm) cmd += " -fverbose-asm";
    } else if (o.dump == Dump::LlvmIr) {
        cmd += " -S -emit-llvm";
    } else {
        cmd += " -E";
    }

    cmd += " " + quote(cpp_path.string()) + " -o " + quote(out_tmp.string());
    if (o.verbose) std::cerr << cmd << "\n";

    if (exit_status_of(run_command(cmd)) != 0) {
        std::cerr << "walnut: the C++ compiler failed to make the " << (o.dump == Dump::Assembly ? "assembly" : (o.dump == Dump::LlvmIr ? "LLVM IR" : "preprocessed C++")) << "; the generated C++ is in " << tmp.string() << "\n";
        return 1;
    }

    bool ok = true;
    const std::string text = filter_dump(o, walnut::build::read_text(out_tmp), ok);
    std::error_code ec;
    if (!ok && text.empty()) { fs::remove_all(tmp, ec); return 1; }
    fs::path out;

    if (!o.output.empty()) out = o.output;
    else if (o.command == Command::Build) out = fs::path(o.input).filename().replace_extension(ext);

    int status = ok ? 0 : 1;
    if (out.empty()) std::cout << text;
    else if (!write_file(out, text)) status = 1;
    fs::remove_all(tmp, ec);
    return status;
}

} // namespace walnut_cli
