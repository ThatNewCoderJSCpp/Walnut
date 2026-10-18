#include "Semantics/const_evaluator.hpp"
#include "Semantics/compile_time_random.hpp"

#include <string>
#include <vector>

namespace walnut {
namespace semantics {

namespace {

using BK = parser_types::PrimitiveType::BaseKind;

std::uint64_t mix_hash(std::uint64_t h, std::uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

std::uint64_t site_of(const nodes::ASTNode* n) {
    if (!n) return 0;
    std::uint64_t h = 0x243f6a8885a308d3ULL;
    h = mix_hash(h, static_cast<std::uint64_t>(n->file_id));
    h = mix_hash(h, static_cast<std::uint64_t>(n->line));
    h = mix_hash(h, static_cast<std::uint64_t>(n->kind));
    if (n->kind == nodes::ASTNode::Kind::VariableDeclaration) {
        for (unsigned char c : static_cast<const nodes::VariableDeclaration*>(n)->m_name) h = mix_hash(h, c);
    }
    if (!compile_time_random_is_seeded()) h = mix_hash(h, static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(n)));
    return h;
}

WideInt word_base() { return WideInt(std::uint64_t(1) << 32) * WideInt(std::uint64_t(1) << 32); }

std::size_t words_for(const WideInt& span) {
    const WideInt base = word_base();
    WideInt left = span;
    std::size_t n = 0;
    while (!left.is_zero()) {
        left = left / base;
        ++n;
    }
    return n;
}

WideInt random_words(RandomStream& rs, std::size_t count, const WideInt& base) {
    WideInt r(std::uint64_t(0));
    for (std::size_t i = 0; i < count; ++i) r = r * base + WideInt(rs.next64());
    return r;
}

WideInt below(RandomStream& rs, const WideInt& bound) {
    if (bound <= WideInt(std::uint64_t(1))) return WideInt(std::uint64_t(0));
    const WideInt base = word_base();
    const std::size_t words = words_for(bound);
    if (words <= 1) {
        std::uint64_t b = 0;
        WideInt left = bound;
        b = static_cast<std::uint64_t>(std::stoull(left.to_string()));
        return WideInt(rs.below(b));
    }
    const std::size_t count = words + 1 > 32 ? 32 : words + 1;
    return random_words(rs, count, base) % bound;
}

std::vector<std::uint32_t> decode_utf8(const std::string& s, bool& ok) {
    std::vector<std::uint32_t> out;
    ok = true;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::uint32_t cp = 0;
        std::size_t len = 0;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1F; len = 2; }
        else if ((c >> 4) == 0xE) { cp = c & 0x0F; len = 3; }
        else if ((c >> 3) == 0x1E) { cp = c & 0x07; len = 4; }
        else { ok = false; return out; }
        if (i + len > s.size()) { ok = false; return out; }
        for (std::size_t k = 1; k < len; ++k) {
            const unsigned char d = static_cast<unsigned char>(s[i + k]);
            if ((d >> 6) != 0x2) { ok = false; return out; }
            cp = (cp << 6) | (d & 0x3F);
        }
        out.push_back(cp);
        i += len;
    }
    return out;
}

void encode_utf8(std::uint32_t cp, std::string& out) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
}

const std::string& alphanumeric() {
    static const std::string s = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    return s;
}

BuiltinType* builtin_of(TypeContext& types, Type* t) {
    if (!t) return nullptr;
    t = types.apply_subst(t);
    if (t && t->is_reference()) t = static_cast<ReferenceType*>(t)->referent();
    t = types.strip_cv(t);
    return t && t->is_builtin() ? static_cast<BuiltinType*>(t) : nullptr;
}

} // namespace

ConstEvaluator::RandomPermit::RandomPermit(ConstEvaluator& evaluator, const nodes::ASTNode* root) : ev(evaluator) {
    RandomScope scope;
    scope.site = site_of(root);
    ev.m_random_scopes.push_back(scope);
}

ConstEvaluator::RandomPermit::~RandomPermit() {
    ev.m_random_scopes.pop_back();
}

bool ConstEvaluator::is_random_function(const Symbol* fn) {
    if (!fn || fn->kind != SymbolKind::Function) return false;
    const std::string_view n = fn->name;
    if (n != "random_int" && n != "random_float" && n != "random_bool" && n != "random_char" && n != "random_string" && n != "random_text") return false;
    const Scope* sc = fn->owner;
    while (sc && sc->kind == Scope::Kind::Template) sc = sc->parent;
    const Symbol* ns = sc ? sc->owner_symbol : nullptr;
    if (!ns || ns->name != "random" || ns->kind != SymbolKind::Namespace) return false;
    const Scope* up = ns->owner;
    const Symbol* top = up ? up->owner_symbol : nullptr;
    return top && top->name == "std";
}

ConstValue ConstEvaluator::eval_random(nodes::CallExpression* c, Symbol* fn) {
    if (m_random_scopes.empty()) return {};
    if (hide_random) {
        hidden_random_hit = true;
        return {};
    }

    std::vector<ConstValue> args;
    for (nodes::ASTNode* a : c->m_arguments) {
        ConstValue v = eval(a);
        if (!v.ok()) return v;
        args.push_back(v);
    }

    RandomScope& scope = m_random_scopes.back();
    RandomStream rs(scope.site, scope.count++, static_cast<std::uint64_t>(c->line));
    ++m_random_draws;
    const std::string_view name = fn->name;
    Type* result = c->expr_type.type;

    if (name == "random_int") {
        BuiltinType* b = builtin_of(m_types, result);
        if (!b || b->base() != BK::Int) return {};
        const unsigned bits = bit_width_of_rank(b->width());
        WideInt lo, hi;
        if (args.size() == 2) {
            if (!args[0].is_int() || !args[1].is_int()) return ConstValue::error(ConstValue::Fail::Domain);
            lo = *args[0].i;
            hi = *args[1].i;
            if (hi < lo) std::swap(lo, hi);
        } else if (args.empty()) {
            if (b->is_unsigned()) {
                lo = WideInt(std::uint64_t(0));
                hi = WideInt(width_mask(bits));
            } else {
                const WideInt half = WideInt(width_mask(bits - 1));
                lo = -half - WideInt(std::uint64_t(1));
                hi = half;
            }
        } else {
            return {};
        }
        const WideInt span = hi - lo;
        if (words_for(span) >= 32) {
            const WideInt r = random_words(rs, 32, word_base());
            const WideInt all = WideInt(width_mask(2048));
            return coerce(make_int(lo + (span < all ? r % (span + WideInt(std::uint64_t(1))) : r)), result);
        }
        return coerce(make_int(lo + below(rs, span + WideInt(std::uint64_t(1)))), result);
    }

    if (name == "random_float") {
        BuiltinType* b = builtin_of(m_types, result);
        if (!b || b->base() != BK::Float) return {};
        WideFloat lo = wf_from_int(WideInt(std::uint64_t(0)));
        WideFloat hi = wf_from_int(WideInt(std::uint64_t(1)));
        if (args.size() == 2) {
            if (args[0].is_special() || args[1].is_special()) return ConstValue::error(ConstValue::Fail::Domain);
            if (!as_wide_float(args[0], lo) || !as_wide_float(args[1], hi)) return {};
            if (hi < lo) std::swap(lo, hi);
        } else if (!args.empty()) {
            return {};
        }
        if (!(lo < hi)) return coerce(make_float(lo), result);
        const unsigned bits = bit_width_of_rank(b->width());
        const std::size_t words = bits / 64 + 2;
        const WideInt base = word_base();
        WideInt scale(std::uint64_t(1));
        for (std::size_t i = 0; i < words; ++i) scale = scale * base;
        const WideFloat u = wf_from_int(random_words(rs, words, base)) / wf_from_int(scale);
        ConstValue v = coerce(make_float(lo + (hi - lo) * u), result);
        WideFloat got;
        if (v.ok() && as_wide_float(v, got) && !(got < hi)) v = coerce(make_float(lo), result);
        return v;
    }

    if (name == "random_bool") {
        if (args.empty()) return make_bool((rs.next32() & 1u) != 0);
        WideFloat p;
        if (args.size() != 1 || !as_wide_float(args[0], p)) return {};
        const WideFloat u = wf_from_int(WideInt(rs.next64())) / (wf_from_int(word_base()));
        return make_bool(u < p);
    }

    if (name == "random_char") {
        if (args.empty()) {
            const std::string& set = alphanumeric();
            return coerce(make_int(WideInt(std::uint64_t(static_cast<unsigned char>(set[rs.below(set.size())])))), result);
        }
        if (args.size() == 2) {
            if (!args[0].is_int() || !args[1].is_int()) return {};
            WideInt lo = *args[0].i, hi = *args[1].i;
            if (hi < lo) std::swap(lo, hi);
            const WideInt top(std::uint64_t(0x10FFFF));
            if (lo.is_negative() || top < hi) return ConstValue::error(ConstValue::Fail::Domain);
            const std::uint64_t a = std::stoull(lo.to_string());
            const std::uint64_t z = std::stoull(hi.to_string());
            const std::uint64_t s_lo = a > 0xD800 ? a : 0xD800;
            const std::uint64_t s_hi = z < 0xDFFF ? z : 0xDFFF;
            const std::uint64_t skipped = s_lo <= s_hi ? s_hi - s_lo + 1 : 0;
            const std::uint64_t count = z - a + 1 - skipped;
            if (count == 0) return ConstValue::error(ConstValue::Fail::Domain);
            std::uint64_t pick = a + rs.below(count);
            if (skipped && pick >= s_lo) pick += skipped;
            return coerce(make_int(WideInt(pick)), result);
        }
        if (args.size() == 1 && args[0].is_string()) {
            bool ok = false;
            const std::vector<std::uint32_t> cps = decode_utf8(*args[0].s, ok);
            if (!ok || cps.empty()) return ConstValue::error(ConstValue::Fail::Domain);
            return coerce(make_int(WideInt(std::uint64_t(cps[rs.below(cps.size())]))), result);
        }
        return {};
    }

    if (name == "random_string" || name == "random_text") {
        if (args.empty() || !args[0].is_int() || args[0].i->is_negative()) return ConstValue::error(ConstValue::Fail::Domain);
        if (WideInt(std::uint64_t(1) << 20) < *args[0].i) return ConstValue::error(ConstValue::Fail::StepLimit);
        const std::uint64_t length = std::stoull(args[0].i->to_string());
        std::vector<std::uint32_t> cps;
        if (args.size() >= 2) {
            if (!args[1].is_string()) return {};
            bool ok = false;
            cps = decode_utf8(*args[1].s, ok);
            if (!ok || cps.empty()) return ConstValue::error(ConstValue::Fail::Domain);
        } else {
            for (char ch : alphanumeric()) cps.push_back(static_cast<unsigned char>(ch));
        }
        std::string out;
        for (std::uint64_t i = 0; i < length; ++i) encode_utf8(cps[rs.below(cps.size())], out);
        return make_string(m_types.intern_string(out));
    }

    return {};
}

} // namespace semantics
} // namespace walnut
