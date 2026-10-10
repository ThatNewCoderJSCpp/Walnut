#ifndef WALNUT_PREPROCESSOR_HPP
#define WALNUT_PREPROCESSOR_HPP

#include "../Lexer/token_macro.hpp"
#include "../Lexer/token_stream.hpp"
#include "../Lexer/lexer.hpp"
#include "../Common/source_manager.hpp"
#include "../Common/error_reporter.hpp"
#include "../Common/arena_allocator.hpp"
#include "../Common/profiler.hpp"
#include "../Common/compiler_warning.hpp"
#include "../Common/small_vector.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <optional>
#include <algorithm>
#include <iterator>
#include <functional>
#include <new>

namespace walnut {
namespace preprocessing {

using tokenizing::Token;
using Kind = tokenizing::Token::Kind;
inline constexpr unsigned int MAX_INCLUDE_DEPTH = 256;

using HideSet = std::vector<std::string>;
using HSPtr   = std::shared_ptr<const HideSet>;

struct PPToken {
    Token tok{};
    HSPtr hs{};
    bool  placemarker = false;
};

} // namespace preprocessing

using TokenVec = walnut::SmallVector<preprocessing::Token, 16>;
using PPVec    = walnut::SmallVector<preprocessing::PPToken, 8>;
using PPArgs   = walnut::SmallVector<PPVec, 4>;

namespace preprocessing {

struct Macro {
    bool                     function_like = false;
    bool                     variadic      = false;
    std::vector<std::string> params;
    std::vector<Token>       body;
    std::uint32_t            def_line = 0;
    FileId                   def_file = INVALID_FILE;
};

class Preprocessor {
public:
    Preprocessor(SourceManager& sm, ErrorReporter& reporter, WarningReporter& warnings, Arena& arena);

    void define(std::string name, std::string value = "1");

    void undefine(const std::string& name) { m_macros.erase(name); }
    void add_include_dir(std::string dir) { m_include_dirs.push_back(std::move(dir)); }

    tokenizing::TokenStream preprocess(FileId main);

private:
    struct Reader {
        const Token* toks = nullptr;
        std::size_t  n    = 0;
        std::size_t  i    = 0;
        const Token& cur() const noexcept { return toks[i]; }
        const Token& peek(std::size_t k = 1) const noexcept { std::size_t j = i + k; return j < n ? toks[j] : toks[n - 1]; }
        bool at_end() const noexcept { return i >= n || toks[i].is(Kind::End); }
        void adv() noexcept { if (i < n) ++i; }
        bool prev_on_same_line() const noexcept { return i > 0 && toks[i].line() == toks[i - 1].line(); }
    };

    void run_file(FileId file, unsigned int depth);

    void handle_directive(Reader& r, FileId file, unsigned int depth);

    void do_define(TokenVec& body, FileId file, std::uint32_t line);

    void do_undef(TokenVec& body, std::uint32_t line);

    void do_include(TokenVec& body, FileId from, std::uint32_t line, unsigned int depth);

    std::optional<FileId> resolve_include(const std::string& path, bool angled, FileId from);

    void do_error(TokenVec& body, std::uint32_t line) { m_reporter.report(ErrorPhase::Preprocessor, line, "#error: " + spell(body)); }
    void do_warning(TokenVec& body, std::uint32_t line) { m_warnings.report(ErrorPhase::Preprocessor, line, "#warning: " + spell(body)); }

    void do_pragma(TokenVec& body, FileId file, std::uint32_t) {
        if (body.empty()) return;
        if (body[0].lexeme() == "once") m_pragma_once.insert(file);
    }

    struct CondFrame {
        bool parent_active;
        bool branch_active;
        bool any_taken;
        bool seen_else;
    };

    bool emitting() const noexcept;

    void do_if(bool cond) {
        bool parent = emitting();
        m_cond.push_back({ parent, parent && cond, parent && cond, false });
    }

    void do_elif(TokenVec& body, std::uint32_t line);

    void do_else(std::uint32_t line);

    void do_endif(std::uint32_t line);

    bool name_defined(TokenVec& body, std::uint32_t line);

    bool eval_condition(TokenVec& raw, std::uint32_t line);

    TokenVec prepare_condition(TokenVec& raw, std::uint32_t line);

    long long parse_ternary(TokenVec& t, std::size_t& p, std::uint32_t line);

    struct BinOp { int prec; int op; };

    BinOp peek_binop(TokenVec& t, std::size_t p);

    long long parse_binary(TokenVec& t, std::size_t& p, int min_prec, std::uint32_t line);

    long long parse_unary(TokenVec& t, std::size_t& p, std::uint32_t line);

    long long parse_primary(TokenVec& t, std::size_t& p, std::uint32_t line);

    long long apply(long long a, long long b, int op, std::uint32_t line);

    bool pull_run_token(Reader* src, PPToken& out);

    PPVec expand(PPVec input, Reader* src = nullptr);

    bool collect_actuals(PPVec& W, Reader* src, PPArgs& actuals, HSPtr& closeHS);

    PPVec subst(const Macro& m, const PPArgs& actuals, const HSPtr& HS, std::uint32_t line, std::uint32_t site_file);

    PPToken glue(const PPToken& l, const PPToken& r, std::uint32_t line);

    PPToken stringize(const PPVec& toks, std::uint32_t line);

    void hsadd(const HSPtr& HS, PPVec& os);

    TokenVec read_logical_line(Reader& r);

    static bool adjacent(const Token& a, const Token& b) noexcept { return a.data() + a.length() == b.data(); }
    static PPToken ppt(const Token& t) { PPToken p; p.tok = t; return p; }
    static PPToken placemarker_tok() { PPToken p; p.placemarker = true; return p; }

    static const PPVec& empty_args() {
        static const PPVec e;
        return e;
    }

    static bool hs_contains(const HSPtr& a, std::string_view n);

    static HSPtr hs_with(const HSPtr& a, std::string_view name);

    static HSPtr hs_union(const HSPtr& a, const HideSet& add);

    static HSPtr hs_intersect(const HSPtr& a, const HSPtr& b);

    std::string join_until(const TokenVec& body, std::size_t start, Kind stop);

    std::string spell(const TokenVec& toks);

    long long parse_int(std::string_view sv);

    std::vector<Token> lex_fragment(const std::string& text);

    void validate_body_operators(const Macro& m, std::uint32_t line);

    const char* intern(std::string_view s);

    Token make_token(Kind k, std::string_view lexeme, std::uint32_t line) {
        const char* p = intern(lexeme);
        return Token(k, p, lexeme.size(), line);
    }

    Token int_token(long long v, std::uint32_t line) { return make_token(Kind::Integer, std::to_string(v), line); }
    Token retarget(const Token& t, std::uint32_t line) { return Token(t.kind(), t.data(), t.length(), line); }
    void emit(const Token& t) { m_out.push_back(t); }

    std::unordered_map<std::string, walnut::preprocessing::Macro>::iterator find_macro(std::string_view name) {
        m_key.assign(name.data(), name.size());
        return m_macros.find(m_key);
    }

    bool is_expandable(const Token& t);
private:
    static bool is_builtin_name(std::string_view lx);

    bool expand_builtin(const Token& t, TokenVec& out);

    void register_platform_defines() {
        register_os();
        register_walnut();
        register_isa();
        register_simd();
    }

    void register_os();

    void register_walnut() {
        define("__WALNUT__", "1");
        define("__WALNUT_VERSION__", "100"); // 100 * major + 10 * minor + patch
    }

    void register_isa();

    void register_simd();

private:
    SourceManager&   m_sm;
    ErrorReporter&   m_reporter;
    WarningReporter& m_warnings;
    Arena&           m_arena;

    std::unordered_map<std::string, Macro> m_macros;
    std::string                            m_key;
    std::vector<std::string>               m_include_dirs;
    std::unordered_set<FileId>             m_pragma_once;
    std::vector<CondFrame>                 m_cond;
    std::vector<Token>                     m_out;
    FileId                                 m_cur_file = INVALID_FILE;
    std::string                            m_empty_file = "<unknown>";
};

} // namespace preprocessing
} // namespace walnut

#endif // WALNUT_PREPROCESSOR_HPP