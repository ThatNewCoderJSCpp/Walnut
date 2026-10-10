#include "Preprocessor/preprocessor.hpp"

namespace walnut {
namespace preprocessing {

Preprocessor::Preprocessor(SourceManager& sm, ErrorReporter& reporter, WarningReporter& warnings, Arena& arena) : m_sm(sm), m_reporter(reporter), m_warnings(warnings), m_arena(arena) { register_platform_defines(); }

void Preprocessor::define(std::string name, std::string value) {
    Macro m;
    m.function_like = false;
    if (!value.empty()) m.body = lex_fragment(value);
    m_macros[std::move(name)] = std::move(m);
}

tokenizing::TokenStream Preprocessor::preprocess(FileId main) {
    m_out.clear();
    m_out.reserve(m_sm.size(main) / 5 + 64);
    m_pragma_once.clear();
    m_cond.clear();
    m_cur_file = main;
    run_file(main, 0);
    m_out.push_back(Token(Kind::End, m_out.empty() ? 0u : m_out.back().line()));
    const std::size_t n = m_out.size();
    Token* buf = m_arena.alloc_array_uninit<Token>(n);
    for (std::size_t i = 0; i < n; ++i) ::new (static_cast<void*>(buf + i)) Token(m_out[i]);
    return tokenizing::TokenStream(buf, n);
}

void Preprocessor::run_file(FileId file, unsigned int depth) {
    if (depth > MAX_INCLUDE_DEPTH) {
        m_reporter.report(ErrorPhase::Preprocessor, 0, "#include nesting too deep (cycle?)");
        return;
    }

    if (m_pragma_once.count(file)) return;
    const FileId prev_file = m_cur_file;
    m_cur_file = file;
    m_sm.lex(file, m_reporter);
    const Token* toks = m_sm.cached_tokens(file);
    const std::size_t n = m_sm.cached_token_count(file);
    Reader r{ toks, n, 0 };
    m_out.reserve(m_out.size() + n);
    const std::size_t cond_base = m_cond.size();

    while (!r.at_end()) {
        const Token& t = r.cur();

        if (t.is(Kind::Hash) && !r.prev_on_same_line()) {
            handle_directive(r, file, depth);
            continue;
        }

        if (!emitting()) { r.adv(); continue; }
        if (!is_expandable(t)) { emit(t); r.adv(); continue; }
        PPVec seed;
        seed.push_back(ppt(t));
        r.adv();
        PPVec expanded = expand(std::move(seed), &r);
        for (PPToken& pt : expanded) if (!pt.placemarker) emit(pt.tok);
    }

    if (m_cond.size() != cond_base) {
        m_reporter.report(ErrorPhase::Preprocessor, 0, "unterminated #if (missing #endif)");
        m_cond.resize(cond_base);
    }

    m_cur_file = prev_file;
}

void Preprocessor::handle_directive(Reader& r, FileId file, unsigned int depth) {
    const std::uint32_t line = r.cur().line();
    TokenVec rest = read_logical_line(r);
    if (rest.empty()) return;
    const std::string_view name = rest[0].lexeme();
    TokenVec body(rest.begin() + 1, rest.end());
    if (name == "if")     { do_if(eval_condition(body, line));    return; }
    if (name == "ifdef")  { do_if(name_defined(body, line));      return; }
    if (name == "ifndef") { do_if(!name_defined(body, line));     return; }
    if (name == "elif")   { do_elif(body, line);                  return; }
    if (name == "else")   { do_else(line);                        return; }
    if (name == "endif")  { do_endif(line);                       return; }
    if (!emitting()) return;
    if (name == "define")  { do_define(body, file, line);         return; }
    if (name == "undef")   { do_undef(body, line);                return; }
    if (name == "include") { do_include(body, file, line, depth); return; }
    if (name == "error")   { do_error(body, line);                return; }
    if (name == "warning") { do_warning(body, line);              return; }
    if (name == "pragma")  { do_pragma(body, file, line);         return; }
    if (name == "line")    { return; }
    m_reporter.report(ErrorPhase::Preprocessor, line, "unknown directive #" + std::string(name));
}

void Preprocessor::do_define(TokenVec& body, FileId file, std::uint32_t line) {
    if (body.empty() || !body[0].is(Kind::Identifier)) {
        m_reporter.report(ErrorPhase::Preprocessor, line, "#define requires a name");
        return;
    }

    std::string mname(body[0].lexeme());
    Macro m;
    m.def_line = line;
    m.def_file = file;
    std::size_t k = 1;

    if (k < body.size() && body[k].is(Kind::LeftParen) && adjacent(body[0], body[k])) {
        m.function_like = true;
        ++k;
        bool expect_param = true;

        while (k < body.size() && !body[k].is(Kind::RightParen)) {
            const Token& p = body[k];
            if (p.is(Kind::Ellipsis)) { m.variadic = true; ++k; break; }

            if (expect_param) {
                if (!p.is(Kind::Identifier)) {
                    m_reporter.report(ErrorPhase::Preprocessor, line, "bad macro parameter");
                    break;
                }

                m.params.emplace_back(p.lexeme());
                expect_param = false;
            } else if (!p.is(Kind::Comma)) {
                m_reporter.report(ErrorPhase::Preprocessor, line, "expected ',' in macro parameters");
            }

            if (p.is(Kind::Comma)) expect_param = true;
            ++k;
        }

        if (k < body.size() && body[k].is(Kind::RightParen)) ++k;
    }

    m.body.assign(body.begin() + k, body.end());
    validate_body_operators(m, line);
    m_macros[std::move(mname)] = std::move(m);
}

void Preprocessor::do_undef(TokenVec& body, std::uint32_t line) {
    if (body.empty() || !body[0].is(Kind::Identifier)) {
        m_reporter.report(ErrorPhase::Preprocessor, line, "#undef requires a name");
        return;
    }

    m_macros.erase(std::string(body[0].lexeme()));
}

void Preprocessor::do_include(TokenVec& body, FileId from, std::uint32_t line, unsigned int depth) {
    std::string path;
    bool angled = false;

    if (!body.empty() && body[0].is(Kind::String)) {
        path = std::string(body[0].lexeme());
    } else if (!body.empty() && body[0].is(Kind::LessThan)) {
        angled = true;
        path = join_until(body, 1, Kind::GreaterThan);
    } else {
        m_reporter.report(ErrorPhase::Preprocessor, line, "#include expects \"file\" or <file>");
        return;
    }

    std::optional<FileId> target = resolve_include(path, angled, from);

    if (!target) {
        m_reporter.report(ErrorPhase::Preprocessor, line, "cannot open include: " + path);
        return;
    }

    run_file(*target, depth + 1);
}

std::optional<FileId> Preprocessor::resolve_include(const std::string& path, bool angled, FileId from) {
    namespace fs = std::filesystem;

    if (!angled) {
        fs::path base = fs::path(m_sm.absolute_path(from)).parent_path();
        if (auto id = m_sm.load((base / path).string())) return id;
    }

    for (const std::string& dir : m_include_dirs) if (auto id = m_sm.load((fs::path(dir) / path).string())) return id;
    return m_sm.load(path);
}

bool Preprocessor::emitting() const noexcept {
    for (const CondFrame& f : m_cond) if (!f.branch_active || !f.parent_active) return false;
    return true;
}

void Preprocessor::do_elif(TokenVec& body, std::uint32_t line) {
    if (m_cond.empty()) {
        m_reporter.report(ErrorPhase::Preprocessor, line, "#elif without #if");
        return;
    }

    CondFrame& f = m_cond.back();

    if (f.seen_else) {
        m_reporter.report(ErrorPhase::Preprocessor, line, "#elif after #else");
        return;
    }

    if (f.any_taken) { f.branch_active = false; return; }
    bool cond = f.parent_active && eval_condition(body, line);
    f.branch_active = cond;
    f.any_taken = f.any_taken || cond;
}

void Preprocessor::do_else(std::uint32_t line) {
    if (m_cond.empty()) {
        m_reporter.report(ErrorPhase::Preprocessor, line, "#else without #if");
        return;
    }

    CondFrame& f = m_cond.back();
    if (f.seen_else) m_reporter.report(ErrorPhase::Preprocessor, line, "duplicate #else");
    f.seen_else = true;
    f.branch_active = f.parent_active && !f.any_taken;
    f.any_taken = true;
}

void Preprocessor::do_endif(std::uint32_t line) {
    if (m_cond.empty()) {
        m_reporter.report(ErrorPhase::Preprocessor, line, "#endif without #if");
        return;
    }

    m_cond.pop_back();
}

bool Preprocessor::name_defined(TokenVec& body, std::uint32_t line) {
    if (body.empty() || !body[0].is(Kind::Identifier)) {
        m_reporter.report(ErrorPhase::Preprocessor, line, "#ifdef/#ifndef needs a name");
        return false;
    }

    return find_macro(body[0].lexeme()) != m_macros.end();
}

bool Preprocessor::eval_condition(TokenVec& raw, std::uint32_t line) {
    TokenVec prepared = prepare_condition(raw, line);
    std::size_t pos = 0;
    long long v = parse_ternary(prepared, pos, line);
    if (pos != prepared.size()) m_reporter.report(ErrorPhase::Preprocessor, line, "trailing tokens in #if expression");
    return v != 0;
}

TokenVec Preprocessor::prepare_condition(TokenVec& raw, std::uint32_t line) {
    TokenVec step1;
    step1.reserve(raw.size());

    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i].is(Kind::Identifier) && raw[i].lexeme() == "defined") {
            std::string_view nm;

            if (i + 1 < raw.size() && raw[i + 1].is(Kind::LeftParen)) {
                if (
                    i + 2 < raw.size() && raw[i + 2].is(Kind::Identifier) &&
                    i + 3 < raw.size() && raw[i + 3].is(Kind::RightParen)
                ) {
                    nm = raw[i + 2].lexeme(); i += 3;
                } else {
                    m_reporter.report(ErrorPhase::Preprocessor, line, "malformed defined()");
                }
            } else if (i + 1 < raw.size() && raw[i + 1].is(Kind::Identifier)) {
                nm = raw[i + 1].lexeme(); i += 1;
            } else {
                m_reporter.report(ErrorPhase::Preprocessor, line, "defined needs an operand");
            }

            step1.push_back(int_token(find_macro(nm) != m_macros.end() ? 1 : 0, line));
        } else {
            step1.push_back(raw[i]);
        }
    }

    PPVec d;
    d.reserve(step1.size());
    for (Token& t : step1) d.push_back(ppt(t));
    PPVec ex = expand(std::move(d));
    TokenVec result;
    result.reserve(ex.size());
    for (PPToken& pt : ex) if (!pt.placemarker) result.push_back(pt.tok);
    return result;
}

long long Preprocessor::parse_ternary(TokenVec& t, std::size_t& p, std::uint32_t line) {
    long long c = parse_binary(t, p, 0, line);

    if (p < t.size() && t[p].is(Kind::QuestionMark)) {
        ++p;
        long long a = parse_ternary(t, p, line);
        if (p < t.size() && t[p].is(Kind::Colon)) ++p;
        else m_reporter.report(ErrorPhase::Preprocessor, line, "expected ':' in #if ternary");
        long long b = parse_ternary(t, p, line);
        return c ? a : b;
    }

    return c;
}

auto Preprocessor::peek_binop(TokenVec& t, std::size_t p) -> BinOp {
    if (p >= t.size()) return { -1, 0 };
    const Token& a = t[p];
    if (a.is(Kind::LogicOr))  return { 1, 'O' };
    if (a.is(Kind::LogicAnd)) return { 2, 'A' };
    if (a.is(Kind::Pipe))      return { 3, '|' };
    if (a.is(Kind::Caret))     return { 4, '^' };
    if (a.is(Kind::Ampersand)) return { 5, '&' };
    if (a.is(Kind::LogicEqual)) return { 6, 'E' };
    if (a.is(Kind::NotEqual))   return { 6, 'N' };
    if (a.is(Kind::LessEqual))    return { 7, 'l' };
    if (a.is(Kind::GreaterEqual)) return { 7, 'g' };
    if (a.is(Kind::LessThan))     return { 7, '<' };
    if (a.is(Kind::GreaterThan))  return { 7, '>' };
    if (a.is(Kind::DoubleLessThan))    return { 8, 'L' };
    if (a.is(Kind::DoubleGreaterThan)) return { 8, 'R' };
    if (a.is(Kind::Plus))  return { 9, '+' };
    if (a.is(Kind::Minus)) return { 9, '-' };
    if (a.is(Kind::Asterisk)) return { 10, '*' };
    if (a.is(Kind::Slash))    return { 10, '/' };
    if (a.is(Kind::Percent))  return { 10, '%' };
    return { -1, 0 };
}

long long Preprocessor::parse_binary(TokenVec& t, std::size_t& p, int min_prec, std::uint32_t line) {
    long long lhs = parse_unary(t, p, line);

    for (;;) {
        BinOp o = peek_binop(t, p);
        if (o.prec < min_prec || o.prec < 0) break;
        ++p;
        long long rhs = parse_binary(t, p, o.prec + 1, line);
        lhs = apply(lhs, rhs, o.op, line);
    }

    return lhs;
}

long long Preprocessor::parse_unary(TokenVec& t, std::size_t& p, std::uint32_t line) {
    if (p < t.size()) {
        if (t[p].is(Kind::ExclamationMark)) { ++p; return !parse_unary(t, p, line); }
        if (t[p].is(Kind::Tilde))           { ++p; return ~parse_unary(t, p, line); }
        if (t[p].is(Kind::Minus))           { ++p; return -parse_unary(t, p, line); }
        if (t[p].is(Kind::Plus))            { ++p; return  parse_unary(t, p, line); }
    }

    return parse_primary(t, p, line);
}

long long Preprocessor::parse_primary(TokenVec& t, std::size_t& p, std::uint32_t line) {
    if (p >= t.size()) {
        m_reporter.report(ErrorPhase::Preprocessor, line, "unexpected end of #if expression");
        return 0;
    }

    const Token& tok = t[p];

    if (tok.is(Kind::LeftParen)) {
        ++p;
        long long v = parse_ternary(t, p, line);
        if (p < t.size() && t[p].is(Kind::RightParen)) ++p;
        else m_reporter.report(ErrorPhase::Preprocessor, line, "expected ')' in #if");
        return v;
    }

    if (tok.is(Kind::Integer)) { ++p; return parse_int(tok.lexeme()); }
    if (tok.is(Kind::True))    { ++p; return 1; }
    if (tok.is(Kind::False))   { ++p; return 0; }
    if (tok.is(Kind::Identifier)) { ++p; return 0; }
    m_reporter.report(ErrorPhase::Preprocessor, line, "invalid token in #if: " + std::string(tok.lexeme()));
    ++p;
    return 0;
}

long long Preprocessor::apply(long long a, long long b, int op, std::uint32_t line) {
    switch (op) {
        case 'O': return (a != 0) || (b != 0);
        case 'A': return (a != 0) && (b != 0);
        case '|': return a | b;
        case '^': return a ^ b;
        case '&': return a & b;
        case 'E': return a == b;
        case 'N': return a != b;
        case 'l': return a <= b;
        case 'g': return a >= b;
        case '<': return a < b;
        case '>': return a > b;
        case 'L': return a << b;
        case 'R': return a >> b;
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/': if (b == 0) { m_reporter.report(ErrorPhase::Preprocessor, line, "division by zero in #if"); return 0; } return a / b;
        case '%': if (b == 0) { m_reporter.report(ErrorPhase::Preprocessor, line, "modulo by zero in #if");   return 0; } return a % b;
    }

    return 0;
}

bool Preprocessor::pull_run_token(Reader* src, PPToken& out) {
    if (!src || src->at_end()) return false;
    const Token& t = src->cur();
    if (t.is(Kind::Hash) && !src->prev_on_same_line()) return false;
    out = ppt(t);
    src->adv();
    return true;
}

PPVec Preprocessor::expand(PPVec input, Reader* src) {
    PPVec out;
    out.reserve(input.size());
    PPVec W;
    W.reserve(input.size());
    for (std::size_t k = input.size(); k-- > 0; ) W.push_back(std::move(input[k]));
    input.clear();

    auto push_front_seq = [&](PPVec& r) {
        for (std::size_t k = r.size(); k-- > 0; ) W.push_back(std::move(r[k]));
    };

    while (!W.empty()) {
        PPToken T = std::move(W.back());
        W.pop_back();
        if (T.placemarker) continue;
        if (!T.tok.is(Kind::Identifier)) { out.push_back(std::move(T)); continue; }
        const std::string_view name = T.tok.lexeme();
        auto it = find_macro(name);

        if (it == m_macros.end()) {
            TokenVec b;

            if (expand_builtin(T.tok, b)) {
                const std::uint32_t sf = T.tok.file_id();

                for (Token& bt : b) {
                    bt.file_id(sf);
                    out.push_back(ppt(bt));
                }
            } else {
                out.push_back(std::move(T));
            }

            continue;
        }

        if (hs_contains(T.hs, name)) { out.push_back(std::move(T)); continue; }
        const Macro& m = it->second;

        if (!m.function_like) {
            HSPtr hs = hs_with(T.hs, name);
            PPVec r = subst(m, {}, hs, T.tok.line(), T.tok.file_id());
            push_front_seq(r);
            continue;
        }

        while (!W.empty() && W.back().placemarker) W.pop_back();
        if (W.empty()) { PPToken nx; if (pull_run_token(src, nx)) W.push_back(std::move(nx)); }
        if (W.empty() || !W.back().tok.is(Kind::LeftParen)) { out.push_back(std::move(T)); continue; }
        W.pop_back();

        PPArgs actuals;
        HSPtr closeHS;

        if (!collect_actuals(W, src, actuals, closeHS)) {
            m_reporter.report(ErrorPhase::Preprocessor, T.tok.line(), "unterminated macro argument list");
            out.push_back(std::move(T));
            continue;
        }

        if (actuals.empty() && (m.params.size() + (m.variadic ? 1u : 0u)) >= 1) actuals.push_back({});

        if (!m.variadic && actuals.size() != m.params.size()) m_reporter.report(ErrorPhase::Preprocessor, T.tok.line(), "macro '" + std::string(name) + "' passed " + std::to_string(actuals.size()) + " args, expected " + std::to_string(m.params.size()));
        else if (m.variadic && actuals.size() < m.params.size()) m_reporter.report(ErrorPhase::Preprocessor, T.tok.line(), "macro '" + std::string(name) + "' needs at least " + std::to_string(m.params.size()) + " args");

        HSPtr hs = hs_with(hs_intersect(T.hs, closeHS), name);
        PPVec r = subst(m, actuals, hs, T.tok.line(), T.tok.file_id());
        push_front_seq(r);
    }

    return out;
}

bool Preprocessor::collect_actuals(PPVec& W, Reader* src, PPArgs& actuals, HSPtr& closeHS) {
    PPVec cur;
    int depth = 1;
    bool any = false;

    for (;;) {
        if (W.empty()) {
            PPToken nx;
            if (!pull_run_token(src, nx)) return false;
            W.push_back(std::move(nx));
        }

        PPToken t = std::move(W.back());
        W.pop_back();
        if (t.tok.is(Kind::LeftParen)) { ++depth; cur.push_back(std::move(t)); }
        else if (t.tok.is(Kind::RightParen)) {
            if (--depth == 0) {
                closeHS = t.hs;
                if (any || !cur.empty()) actuals.push_back(std::move(cur));
                return true;
            }
            cur.push_back(std::move(t));
        }
        else if (t.tok.is(Kind::Comma) && depth == 1) { actuals.push_back(std::move(cur)); cur.clear(); any = true; }
        else { cur.push_back(std::move(t)); any = true; }
    }
}

PPVec Preprocessor::subst(const Macro& m, const PPArgs& actuals, const HSPtr& HS, std::uint32_t line, std::uint32_t site_file) {
    const std::vector<Token>& body = m.body;
    const std::size_t n = body.size();
    PPVec OS;

    auto param_index = [&](std::string_view nm) -> int {
        for (std::size_t k = 0; k < m.params.size(); ++k) if (m.params[k] == nm) return static_cast<int>(k);
        return -1;
    };

    auto is_va = [&](const Token& t) { return m.variadic && t.is(Kind::Identifier) && t.lexeme() == "__VA_ARGS__"; };

    auto paste_op = [&](std::size_t k) {
        return k + 1 < n && body[k].is(Kind::Hash) && body[k + 1].is(Kind::Hash) && adjacent(body[k], body[k + 1]);
    };

    auto lone_hash = [&](std::size_t k) {
        if (!body[k].is(Kind::Hash)) return false;
        bool first_of  = (k + 1 < n && body[k + 1].is(Kind::Hash) && adjacent(body[k], body[k + 1]));
        bool second_of = (k > 0     && body[k - 1].is(Kind::Hash) && adjacent(body[k - 1], body[k]));
        return !first_of && !second_of;
    };

    auto raw_of = [&](int idx) -> const PPVec& {
        if (idx >= 0 && idx < static_cast<int>(actuals.size())) return actuals[idx];
        return empty_args();
    };

    auto raw_va = [&]() -> PPVec {
        PPVec v;

        for (std::size_t a = m.params.size(); a < actuals.size(); ++a) {
            if (a > m.params.size()) v.push_back(ppt(make_token(Kind::Comma, ",", line)));
            for (const PPToken& pt : actuals[a]) v.push_back(pt);
        }

        return v;
    };

    auto glue_back = [&](PPToken r) {
        if (OS.empty()) { OS.push_back(std::move(r)); return; }
        PPToken l = OS.back();
        OS.pop_back();
        OS.push_back(glue(l, r, line));
    };

    std::size_t i = 0;

    while (i < n) {
        const Token& t = body[i];

        if (lone_hash(i) && i + 1 < n && body[i + 1].is(Kind::Identifier)) {
            int pidx = param_index(body[i + 1].lexeme());
            if (pidx >= 0)            { OS.push_back(stringize(raw_of(pidx), line)); i += 2; continue; }
            if (is_va(body[i + 1]))   { OS.push_back(stringize(raw_va(), line));     i += 2; continue; }
        }

        if (paste_op(i)) {
            std::size_t j = i + 2;
            if (j >= n) { i = j; continue; }
            int ridx = param_index(body[j].lexeme());

            if (body[j].is(Kind::Identifier) && ridx >= 0) {
                const PPVec& ap = raw_of(ridx);
                if (ap.empty()) glue_back(placemarker_tok());
                else { glue_back(ap[0]); for (std::size_t z = 1; z < ap.size(); ++z) OS.push_back(ap[z]); }
            } else if (is_va(body[j])) {
                PPVec ap = raw_va();
                if (ap.empty()) glue_back(placemarker_tok());
                else { glue_back(ap[0]); for (std::size_t z = 1; z < ap.size(); ++z) OS.push_back(ap[z]); }
            } else {
                glue_back(ppt(body[j]));
            }

            i = j + 1;
            continue;
        }

        int idx = body[i].is(Kind::Identifier) ? param_index(body[i].lexeme()) : -1;

        if (idx >= 0 || is_va(t)) {
            bool followed_by_paste = (i + 1 < n && paste_op(i + 1));

            if (followed_by_paste) {
                if (is_va(t)) {
                    PPVec ap = raw_va();
                    if (ap.empty()) OS.push_back(placemarker_tok());
                    else for (PPToken& pt : ap) OS.push_back(std::move(pt));
                } else {
                    const PPVec& ap = raw_of(idx);
                    if (ap.empty()) OS.push_back(placemarker_tok());
                    else for (const PPToken& pt : ap) OS.push_back(pt);
                }
            } else {
                PPVec ex = is_va(t) ? expand(raw_va()) : expand(raw_of(idx));
                for (PPToken& pt : ex) OS.push_back(std::move(pt));
            }

            ++i;
            continue;
        }

        OS.push_back(ppt(t));
        ++i;
    }

    hsadd(HS, OS);
    for (PPToken& pt : OS) if (!pt.placemarker) pt.tok.file_id(site_file);
    return OS;
}

PPToken Preprocessor::glue(const PPToken& l, const PPToken& r, std::uint32_t line) {
    if (l.placemarker && r.placemarker) return placemarker_tok();
    if (l.placemarker) return r;
    if (r.placemarker) return l;
    std::string joined;
    joined.reserve(l.tok.length() + r.tok.length());
    joined.append(l.tok.lexeme());
    joined.append(r.tok.lexeme());
    std::vector<Token> toks = lex_fragment(joined);
    HSPtr hs = hs_intersect(l.hs, r.hs);
    PPToken p;
    p.hs = hs;
    if (toks.size() == 1) { p.tok = retarget(toks[0], line); return p; }
    m_reporter.report(ErrorPhase::Preprocessor, line, "'##' did not form a single valid token: " + joined);
    p.tok = make_token(Kind::Identifier, joined, line);
    return p;
}

PPToken Preprocessor::stringize(const PPVec& toks, std::uint32_t line) {
    std::string s = "\"";
    bool first = true;

    for (const PPToken& pt : toks) {
        if (pt.placemarker) continue;
        if (!first) s += ' ';
        first = false;
        for (char c : pt.tok.lexeme()) { if (c == '"' || c == '\\') s += '\\'; s += c; }
    }

    s += '"';
    PPToken p;
    p.tok = make_token(Kind::String, s, line);
    return p;
}

void Preprocessor::hsadd(const HSPtr& HS, PPVec& os) {
    if (!HS || HS->empty()) return;

    for (PPToken& pt : os) {
        if (pt.placemarker) continue;
        pt.hs = hs_union(pt.hs, *HS);
    }
}

TokenVec Preprocessor::read_logical_line(Reader& r) {
    TokenVec line;
    std::uint32_t cur_line = r.cur().line();
    r.adv();

    while (!r.at_end()) {
        const Token& t = r.cur();

        if (t.line() != cur_line) {
            if (!line.empty() && line.back().is(Kind::BackSlash)) {
                line.pop_back();
                cur_line = t.line();
                continue;
            }

            break;
        }

        line.push_back(t);
        r.adv();
    }

    return line;
}

bool Preprocessor::hs_contains(const HSPtr& a, std::string_view n) {
    if (!a) return false;
    return std::binary_search(a->begin(), a->end(), n, [](std::string_view x, std::string_view y) { return x < y; });
}

HSPtr Preprocessor::hs_with(const HSPtr& a, std::string_view name) {
    HideSet s = a ? *a : HideSet{};
    auto pos = std::lower_bound(s.begin(), s.end(), name, [](std::string_view x, std::string_view y) { return x < y; });
    if (pos == s.end() || std::string_view(*pos) != name) s.insert(pos, std::string(name));
    return std::make_shared<const HideSet>(std::move(s));
}

HSPtr Preprocessor::hs_union(const HSPtr& a, const HideSet& add) {
    if (add.empty()) return a;
    if (!a || a->empty()) return std::make_shared<const HideSet>(add);
    HideSet s;
    s.reserve(a->size() + add.size());
    std::set_union(a->begin(), a->end(), add.begin(), add.end(), std::back_inserter(s));
    return std::make_shared<const HideSet>(std::move(s));
}

HSPtr Preprocessor::hs_intersect(const HSPtr& a, const HSPtr& b) {
    if (!a || !b || a->empty() || b->empty()) return nullptr;
    HideSet s;
    std::set_intersection(a->begin(), a->end(), b->begin(), b->end(), std::back_inserter(s));
    if (s.empty()) return nullptr;
    return std::make_shared<const HideSet>(std::move(s));
}

std::string Preprocessor::join_until(const TokenVec& body, std::size_t start, Kind stop) {
    std::string s;

    for (std::size_t i = start; i < body.size(); ++i) {
        if (body[i].is(stop)) break;
        s.append(body[i].lexeme());
    }

    return s;
}

std::string Preprocessor::spell(const TokenVec& toks) {
    std::string s;

    for (std::size_t i = 0; i < toks.size(); ++i) {
        if (i) s += ' ';
        s.append(toks[i].lexeme());
    }

    return s;
}

long long Preprocessor::parse_int(std::string_view sv) {
    std::string s(sv);

    while (!s.empty()) {
        char c = s.back();
        if (c == 'u' || c == 'U' || c == 'l' || c == 'L') s.pop_back();
        else break;
    }

    if (s.empty()) return 0;
    int base = 10; std::size_t off = 0;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; off = 2; }
    else if (s.size() > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) { base = 2; off = 2; }
    else if (s.size() > 1 && s[0] == '0') { base = 8; off = 1; }
    long long v = 0;

    for (std::size_t i = off; i < s.size(); ++i) {
        char c = s[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * base + d;
    }

    return v;
}

std::vector<Token> Preprocessor::lex_fragment(const std::string& text) {
    const char* src = intern(text);
    tokenizing::Lexer lx(src, m_reporter);
    std::vector<Token> out;

    for (Token t = lx.next(); !t.is(Kind::End); t = lx.next()) {
        if (t.is(Kind::Comment) || t.is(Kind::LongComment)) continue;
        out.push_back(t);
    }

    return out;
}

void Preprocessor::validate_body_operators(const Macro& m, std::uint32_t line) {
    const std::size_t n = m.body.size();
    if (n >= 2 && m.body[0].is(Kind::Hash) && m.body[1].is(Kind::Hash) && adjacent(m.body[0], m.body[1])) m_reporter.report(ErrorPhase::Preprocessor, line, "'##' cannot start a macro body");
    if (n >= 2 && m.body[n - 1].is(Kind::Hash) && m.body[n - 2].is(Kind::Hash) && adjacent(m.body[n - 2], m.body[n - 1])) m_reporter.report(ErrorPhase::Preprocessor, line, "'##' cannot end a macro body");
}

const char* Preprocessor::intern(std::string_view s) {
    char* p = static_cast<char*>(m_arena.alloc_raw(s.size() + 1, alignof(char)));
    if (!s.empty()) std::memcpy(p, s.data(), s.size());
    p[s.size()] = '\0';
    return p;
}

bool Preprocessor::is_expandable(const Token& t) {
    if (!t.is(Kind::Identifier)) return false;
    const std::string_view name = t.lexeme();
    if (find_macro(name) != m_macros.end()) return true;
    return is_builtin_name(name);
}

bool Preprocessor::is_builtin_name(std::string_view lx) {
    return lx == "__LINE__" || lx == "__FILE__" || lx == "__FILE_PATH__" || lx == "__FILE_RELATIVE_PATH__";
}

bool Preprocessor::expand_builtin(const Token& t, TokenVec& out) {
    if (!t.is(Kind::Identifier)) return false;
    const std::string_view lx = t.lexeme();

    if (lx == "__LINE__") {
        out.push_back(int_token(static_cast<long long>(t.line()), t.line()));
        return true;
    }

    if (lx == "__FILE__") {
        const std::string& fname = (m_cur_file == INVALID_FILE) ? m_empty_file : m_sm.filename(m_cur_file);
        out.push_back(make_token(Kind::String, fname, t.line()));
        return true;
    }

    if (lx == "__FILE_PATH__") {
        const std::string& fpath = (m_cur_file == INVALID_FILE) ? m_empty_file : m_sm.absolute_path(m_cur_file);
        out.push_back(make_token(Kind::String, fpath, t.line()));
        return true;
    }

    if (lx == "__FILE_RELATIVE_PATH__") {
        const std::string& fpath = (m_cur_file == INVALID_FILE) ? m_empty_file : m_sm.relative_path(m_cur_file);
        out.push_back(make_token(Kind::String, fpath, t.line()));
        return true;
    }

    return false;
}

void Preprocessor::register_os() {
    #if defined(_WIN32) || defined(_WIN64)
        define("__OS_WINDOWS__");
        define("__OS__", "\"Windows\"");
    #elif defined(__APPLE__) && defined(__MACH__)
        define("__OS_MACOS__");
        define("__OS__", "\"macOS\"");
    #elif defined(__ANDROID__)
        define("__OS_ANDROID__");
        define("__OS__", "\"Android\"");
    #elif defined(__linux__)
        define("__OS_LINUX__");
        define("__OS__", "\"Linux\"");
    #elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
        define("__OS_BSD__");
        define("__OS__", "\"BSD\"");
    #elif defined(__sun) && defined(__SVR4)
        define("__OS_SOLARIS__");
        define("__OS__", "\"Solaris\"");
    #elif defined(__unix__) || defined(__unix)
        define("__OS_UNIX__");
        define("__OS__", "\"Unix\"");
    #else
        define("__OS__", "\"Unknown\"");
    #endif

    define("__IS_OS_WINDOWS__", find_macro("__OS_WINDOWS__") != m_macros.end() ? "true" : "false");
    define("__IS_OS_MACOS__",   find_macro("__OS_MACOS__")   != m_macros.end() ? "true" : "false");
    define("__IS_OS_ANDROID__", find_macro("__OS_ANDROID__") != m_macros.end() ? "true" : "false");
    define("__IS_OS_LINUX__",   find_macro("__OS_LINUX__")   != m_macros.end() ? "true" : "false");
    define("__IS_OS_BSD__",     find_macro("__OS_BSD__")     != m_macros.end() ? "true" : "false");
    define("__IS_OS_SOLARIS__", find_macro("__OS_SOLARIS__") != m_macros.end() ? "true" : "false");
    define("__IS_OS_UNIX__",    find_macro("__OS_UNIX__")    != m_macros.end() ? "true" : "false");
}

void Preprocessor::register_isa() {
#if defined(__aarch64__) || defined(_M_ARM64)
    define("__ARCH_ARM64__");
    define("__ARCH__", "\"ARM64\"");
#elif defined(__arm__) || defined(_M_ARM)
    define("__ARCH_ARM32__");
    define("__ARCH__", "\"ARM32\"");

    #if defined(__ARM_ARCH) && (__ARM_ARCH < 7)
        define("__ARCH_ARM_LEGACY__");
    #else
        define("__ARCH_ARM_V7_OR_GREATER__");
    #endif
#elif defined(__x86_64__) || defined(_M_X64)
    define("__ARCH_X86_64__");
    define("__ARCH__", "\"x86_64\"");
#elif defined(__i386__) || defined(_M_IX86)
    define("__ARCH_X86_32__");
    define("__ARCH__", "\"x86_32\"");
#else
    define("__ARCH__", "\"Unknown\"");
#endif

    define("__IS_ARCH_ARM_LEGACY__",        find_macro("__ARCH_ARM_LEGACY__")        != m_macros.end() ? "true" : "false");
    define("__IS_ARCH_ARM_V7_OR_GREATER__", find_macro("__ARCH_ARM_V7_OR_GREATER__") != m_macros.end() ? "true" : "false");
    define("__IS_ARCH_ARM64__",             find_macro("__ARCH_ARM64__")             != m_macros.end() ? "true" : "false");
    define("__IS_ARCH_ARM32__",             find_macro("__ARCH_ARM32__")             != m_macros.end() ? "true" : "false");
    define("__IS_ARCH_X86_64__",            find_macro("__ARCH_X86_64__")            != m_macros.end() ? "true" : "false");
    define("__IS_ARCH_X86_32__",            find_macro("__ARCH_X86_32__")            != m_macros.end() ? "true" : "false");
}

void Preprocessor::register_simd() {
    // x86_64
#if defined(__SSE__)
    define("__SIMD_SSE__");
#endif
#if defined(__SSE2__)
    define("__SIMD_SSE2__");
#endif
#if defined(__SSE3__)
    define("__SIMD_SSE3__");
#endif
#if defined(__SSSE3__)
    define("__SIMD_SSSE3__");
#endif
#if defined(__SSE4_1__)
    define("__SIMD_SSE4_1__");
#endif
#if defined(__SSE4_2__)
    define("__SIMD_SSE4_2__");
#endif
#if defined(__AVX__)
    define("__SIMD_AVX__");
#endif
#if defined(__AVX2__)
    define("__SIMD_AVX2__");
#endif
#if defined(__AVX512F__)
    define("__SIMD_AVX512F__");
#endif
#if defined(__AVX512BW__)
    define("__SIMD_AVX512BW__");
#endif
#if defined(__AVX512DQ__)
    define("__SIMD_AVX512DQ__");
#endif
#if defined(__AVX512VL__)
    define("__SIMD_AVX512VL__");
#endif

    // ARM / AArch64
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    define("__SIMD_NEON__");
#endif
#if defined(__ARM_FEATURE_SVE)
    define("__SIMD_SVE__");
#endif
#if defined(__ARM_FEATURE_SVE2)
    define("__SIMD_SVE2__");
#endif

    define("__IS_SIMD_SSE__",      find_macro("__SIMD_SSE__")      != m_macros.end() ? "true" : "false");
    define("__IS_SIMD_SSE2__",     find_macro("__SIMD_SSE2__")     != m_macros.end() ? "true" : "false");
    define("__IS_SIMD_SSE3__",     find_macro("__SIMD_SSE3__")     != m_macros.end() ? "true" : "false");
    define("__IS_SIMD_SSSE3__",    find_macro("__SIMD_SSSE3__")    != m_macros.end() ? "true" : "false");
    define("__IS_SIMD_SSE4_1__",   find_macro("__SIMD_SSE4_1__")   != m_macros.end() ? "true" : "false");
    define("__IS_SIMD_SSE4_2__",   find_macro("__SIMD_SSE4_2__")   != m_macros.end() ? "true" : "false");
    define("__IS_SIMD_AVX__",      find_macro("__SIMD_AVX__")      != m_macros.end() ? "true" : "false");
    define("__IS_SIMD_AVX2__",     find_macro("__SIMD_AVX2__")     != m_macros.end() ? "true" : "false");
    define("__IS_SIMD_AVX512F__",  find_macro("__SIMD_AVX512F__")  != m_macros.end() ? "true" : "false");
    define("__IS_SIMD_AVX512BW__", find_macro("__SIMD_AVX512BW__") != m_macros.end() ? "true" : "false");
    define("__IS_SIMD_AVX512DQ__", find_macro("__SIMD_AVX512DQ__") != m_macros.end() ? "true" : "false");
    define("__IS_SIMD_AVX512VL__", find_macro("__SIMD_AVX512VL__") != m_macros.end() ? "true" : "false");

    define("__IS_SIMD_NEON__",     find_macro("__SIMD_NEON__")     != m_macros.end() ? "true" : "false");
    define("__IS_SIMD_SVE__",      find_macro("__SIMD_SVE__")      != m_macros.end() ? "true" : "false");
    define("__IS_SIMD_SVE2__",     find_macro("__SIMD_SVE2__")     != m_macros.end() ? "true" : "false");

    define("__SIMD_X86_SSE_ANY__",
        (
            find_macro("__SIMD_SSE__")    != m_macros.end() ||
            find_macro("__SIMD_SSE2__")   != m_macros.end() ||
            find_macro("__SIMD_SSE3__")   != m_macros.end() ||
            find_macro("__SIMD_SSSE3__")  != m_macros.end() ||
            find_macro("__SIMD_SSE4_1__") != m_macros.end() ||
            find_macro("__SIMD_SSE4_2__") != m_macros.end()
        ) ? "true" : "false"
    );

    define("__SIMD_X86_AVX_ANY__",
        (
            find_macro("__SIMD_AVX__")      != m_macros.end() ||
            find_macro("__SIMD_AVX2__")     != m_macros.end() ||
            find_macro("__SIMD_AVX512F__")  != m_macros.end() ||
            find_macro("__SIMD_AVX512BW__") != m_macros.end() ||
            find_macro("__SIMD_AVX512DQ__") != m_macros.end() ||
            find_macro("__SIMD_AVX512VL__") != m_macros.end()
        ) ? "true" : "false"
    );

    define("__SIMD_X86_AVX512_ANY__",
        (
            find_macro("__SIMD_AVX512F__")  != m_macros.end() ||
            find_macro("__SIMD_AVX512BW__") != m_macros.end() ||
            find_macro("__SIMD_AVX512DQ__") != m_macros.end() ||
            find_macro("__SIMD_AVX512VL__") != m_macros.end()
        ) ? "true" : "false"
    );

    define("__SIMD_X86_ANY__",
        (
            find_macro("__SIMD_X86_SSE_ANY__") != m_macros.end() ||
            find_macro("__SIMD_X86_AVX_ANY__") != m_macros.end()
        ) ? "true" : "false"
    );

    define("__SIMD_ARM_ANY__",
        (
            find_macro("__SIMD_NEON__") != m_macros.end() ||
            find_macro("__SIMD_SVE__")  != m_macros.end() ||
            find_macro("__SIMD_SVE2__") != m_macros.end()
        ) ? "true" : "false"
    );

    define("__SIMD_ARM_SVE_ANY__",
        (
            find_macro("__SIMD_SVE__")  != m_macros.end() ||
            find_macro("__SIMD_SVE2__") != m_macros.end()
        ) ? "true" : "false"
    );
}

} // namespace preprocessing
} // namespace walnut
