#ifndef WALNUT_SEMANTICS_CONSTANT_EVALUATOR_HPP
#define WALNUT_SEMANTICS_CONSTANT_EVALUATOR_HPP

#include "type_impl.hpp"
#include "substitution.hpp"
#include "const_value.hpp"
#include "symbol.hpp"
#include "scope.hpp"

#include "../Parser/nodes.hpp"

#include <unordered_set>
#include <functional>
#include <vector>

namespace walnut {
namespace semantics {

struct ConstValue {
    enum class Kind : std::uint8_t { None = 0, Int, Float, Bool } kind = Kind::None;

    enum class Fail : std::uint8_t {
        NotConstant = 0,   
        
        Overflow,          
        Underflow,         
        Indeterminate,     
        Undefined,         
        Domain,           
        ShiftWidth,  
        Threw,     
        StepLimit,
        NoReturn,
        BadCall,

        Precision,         
        Wrapped          
    } fail = Fail::NotConstant;

    const WideInt*   i = nullptr;
    const WideFloat* f = nullptr;
    bool             b = false;

    bool ok()        const { return kind != Kind::None; }
    bool is_error()  const { return kind == Kind::None && fail != Fail::NotConstant; }
    bool is_int()    const { return kind == Kind::Int; }
    bool is_float()  const { return kind == Kind::Float; }
    bool is_bool()   const { return kind == Kind::Bool; }

    static ConstValue error(Fail f) { ConstValue c; c.fail = f; return c; }
};


inline const char* fail_message(ConstValue::Fail f) {
    switch (f) {
        case ConstValue::Fail::Overflow:      return "value does not fit in its type";
        case ConstValue::Fail::Underflow:     return "value underflows to zero in its type";
        case ConstValue::Fail::Indeterminate: return "indeterminate form";
        case ConstValue::Fail::Undefined:     return "undefined: division by zero";
        case ConstValue::Fail::Domain:        return "argument outside the real domain";
        case ConstValue::Fail::ShiftWidth:    return "shift count out of range";
        case ConstValue::Fail::Precision:     return "result is subnormal; precision lost";
        case ConstValue::Fail::Wrapped:       return "unsigned arithmetic wrapped";
                case ConstValue::Fail::Threw: return "constant evaluation threw an exception";
        case ConstValue::Fail::StepLimit:     return "constant evaluation exceeded its step limit (possible infinite loop or recursion)";
        case ConstValue::Fail::NoReturn:      return "constexpr function finished without returning a value";
        case ConstValue::Fail::BadCall:       return "wrong number of arguments in constant function call";
        default:                              return "not a constant expression";
    }
}

class ConstEvaluator {
public:
    explicit ConstEvaluator(TypeContext& types) : m_types(types) {}

    std::function<const SubstEnv*(Symbol*)> env_of;
    std::function<int(nodes::TemplateInstantiation*)> concept_value;

    ConstValue eval(nodes::ASTNode* e) {
        if (!e || m_depth > kMaxDepth) return {};
        ++m_depth;
        ConstValue v = eval_impl(e);
        --m_depth;
        return v;
    }

    ConstValue value_of(Symbol* s) { return eval_symbol(s); }

    static bool is_constant_type(Type* t) {
        using BK = parser_types::PrimitiveType::BaseKind;
        if (!t || !t->is_builtin()) return false;
        switch (static_cast<BuiltinType*>(t)->base()) {
            case BK::Int: case BK::Float: case BK::Char: case BK::Bool: return true;
            default: return false;
        }
    }

    ConstValue coerce(const ConstValue& v, Type* dst) {
        using BK = parser_types::PrimitiveType::BaseKind;
        if (!v.ok() || !dst) return v;
        dst = m_types.strip_cv(dst);
        if (!is_constant_type(dst)) return v;
        auto* b = static_cast<BuiltinType*>(dst);
        ConstValue out = convert_to(v, b);
        if (out.ok() || out.is_error()) return out;
        if (v.is_int() && (b->base() == BK::Int || b->base() == BK::Char)) return ConstValue::error(ConstValue::Fail::Overflow);
        if (v.is_float() && (b->base() == BK::Int || b->base() == BK::Char)) return ConstValue::error(ConstValue::Fail::Overflow);
        return out;
    }

    static bool truthy(const ConstValue& v, bool& ok) {
        ok = true;
        switch (v.kind) {
            case ConstValue::Kind::Bool:  return v.b;
            case ConstValue::Kind::Int:   return !v.i->is_zero();
            case ConstValue::Kind::Float: return !v.f->is_zero();
            default: ok = false; return false;
        }
    }

    static bool add_wrapped(const WideInt& a, const WideInt& b, const WideInt& r) {
        if (a.is_negative() != b.is_negative()) return false;    
        return r.magnitude() < a.magnitude();                    
    }

    static bool mul_wrapped(const WideInt& a, const WideInt& b, const WideInt& r) {
        if (a.is_zero() || b.is_zero()) return false;
        return (r / b) != a;
    }

    static bool checked_mul(WideInt& acc, const WideInt& x) {
        if (acc.is_zero() || x.is_zero()) { acc = WideInt(); return true; }
        const WideInt r = acc * x;
        if (r / x != acc) return false;
        acc = r;
        return true;
    }

    long long size_of_type(Type* t) {
        long long size = 0, align = 0;
        return layout_of(t, size, align) ? size : 0;
    }

    long long align_of_type(Type* t) {
        long long size = 0, align = 0;
        return layout_of(t, size, align) ? align : 0;
    }

    bool layout_of(Type* t, long long& size, long long& align, int depth = 0) {
        using BK = parser_types::PrimitiveType::BaseKind;
        t = m_types.strip_cv(t);
        if (!t || depth > 64) return false;
        if (t->is_pointer() || t->is_enum()) { size = 8; align = 8; return true; }

        if (t->is_builtin()) {
            auto* b = static_cast<BuiltinType*>(t);
            switch (b->base()) {
                case BK::Int:
                case BK::Float: size = static_cast<long long>(bit_width_of_rank(b->width()) / 8); break;
                case BK::Bool:  size = 1; break;
                case BK::Char:  size = 4; break;
                default:        return false;
            }
            align = size < 8 ? size : 8;
            return true;
        }

        if (t->is_array()) {
            auto* a = static_cast<ArrayType*>(t);
            if (!a->extent()) return false;
            long long es = 0, ea = 0;
            if (!layout_of(a->element(), es, ea, depth + 1)) return false;
            size = es * static_cast<long long>(*a->extent());
            align = ea;
            return true;
        }

        if (t->is_record()) return record_layout(static_cast<RecordType*>(t), size, align, depth);
        return false;
    }

    bool record_layout(RecordType* rt, long long& size, long long& align, int depth) {
        Symbol* owner = rt->decl();
        if (rt->is_instantiation() && m_types.record_scope_hook) {
            Scope* sc = m_types.record_scope_hook(rt);
            if (!sc || !sc->owner_symbol) return false;
            owner = sc->owner_symbol;
        }
        if (!owner || !owner->decl || owner->decl->kind != K::RecordDeclaration) return false;
        auto* rd = static_cast<nodes::RecordDeclaration*>(owner->decl);
        const SubstEnv* env = env_of ? env_of(owner) : nullptr;
        if (env) m_types.push_subst(env);
        struct Pop { TypeContext& t; bool on; ~Pop() { if (on) t.pop_subst(); } } pop{ m_types, env != nullptr };
        long long offset = 0;
        align = 1;
        bool has_vptr = false;

        if (rd->m_inherits.type) {
            long long bs = 0, ba = 0;
            if (!layout_of(m_types.canonicalize(rd->m_inherits), bs, ba, depth + 1)) return false;
            offset = bs;
            align = ba;
        }

        using FQ = modifiers::FunctionQualifiers;
        using RM = modifiers::RawModifiers;

        for (const auto& member : rd->get_members()) {
            nodes::ASTNode* n = member.node;
            if (!n) continue;
            if (n->kind == K::FunctionDeclaration && static_cast<nodes::FunctionDeclaration*>(n)->qualifiers().has_any(FQ::Virtual | FQ::Override)) has_vptr = true;
            if (n->kind == K::OperatorFunctionDeclaration && static_cast<nodes::OperatorFunctionDeclaration*>(n)->qualifiers().has_any(FQ::Virtual | FQ::Override)) has_vptr = true;
            if (n->kind == K::DestructorDeclaration && static_cast<nodes::DestructorDeclaration*>(n)->m_qualifiers.has(FQ::Virtual)) has_vptr = true;
        }

        if (has_vptr && !rd->m_inherits.type) { offset = 8; align = 8; }

        for (const auto& member : rd->get_members()) {
            nodes::ASTNode* n = member.node;
            if (!n) continue;
            Type* ft = nullptr;

            if (n->kind == K::VariableDeclaration) {
                auto* v = static_cast<nodes::VariableDeclaration*>(n);
                if (v->get_type_info().modifiers.has(RM::Static)) continue;
                ft = m_types.canonicalize(v->get_type_info());
            } else if (n->kind == K::ArrayDeclaration) {
                auto* a = static_cast<nodes::ArrayDeclaration*>(n);
                if (a->get_array_modifiers().has(RM::Static)) continue;
                Type* el = m_types.canonicalize(a->m_element_type);
                std::optional<std::size_t> extent = a->m_dimension;
                if (!extent && a->m_initializer && !a->m_dimension_expr) extent = a->m_initializer->m_elements.size();
                if (!extent) return false;
                ft = m_types.array(el, extent, CV{});
            } else {
                continue;
            }

            long long fs = 0, fa = 0;
            if (!ft || ft->is_reference() || !layout_of(ft, fs, fa, depth + 1)) return false;
            offset = (offset + fa - 1) / fa * fa + fs;
            if (fa > align) align = fa;
        }

        size = (offset + align - 1) / align * align;
        if (size == 0) size = 1;
        return true;
    }

    static bool char_fits(const WideInt& v) {
        return !v.is_undefined() && !v.is_negative() && ConstTable::fits(v, 21, false) && !(WideInt(std::uint64_t(0x10FFFF)) < v);
    }

    ConstValue narrow(ConstValue v, nodes::ASTNode* site) {
        using BK = parser_types::PrimitiveType::BaseKind;
        if (!v.ok() || !site) return v;
        Type* t = site->expr_type.type;
        if (!t || !t->is_builtin()) return v;
        auto* b = static_cast<BuiltinType*>(t);
        const unsigned w = bit_width_of_rank(b->width());

        if (v.is_int() && b->base() == BK::Char) {
            return char_fits(*v.i) ? v : ConstValue::error(ConstValue::Fail::Overflow);
        }

        if (v.is_int() && (b->base() == BK::Int || b->base() == BK::Char)) {
            if (!b->is_unsigned()) {
                if (!ConstTable::fits(*v.i, w, true)) return ConstValue::error(ConstValue::Fail::Overflow);
                return v;
            }

            const WideInt wrapped = from_twos(to_twos(*v.i, w), w, false);  
            ConstValue out = make_int(wrapped);
            if (out.ok() && wrapped != *v.i) out.fail = ConstValue::Fail::Wrapped;       
            return out;
        }

        if (v.is_float() && b->base() == BK::Float) {
            WideFloat r;
            if (!wf_round_to_width(*v.f, w, r)) return v;
            if (!v.f->is_infinite() && r.is_infinite()) return ConstValue::error(ConstValue::Fail::Overflow);
            if (!v.f->is_zero()     && r.is_zero())     return ConstValue::error(ConstValue::Fail::Underflow);
            ConstValue out = make_float(r);
            if (out.ok() && r.is_subnormal()) out.fail = ConstValue::Fail::Precision;
            return out;
        }

        return v;
    }

public:
    struct IntShape {
        unsigned bits = 0;
        bool     is_signed = true;
        bool     known() const { return bits != 0; }
    };

    IntShape shape_of(nodes::ASTNode* site) const {
        using BK = parser_types::PrimitiveType::BaseKind;
        IntShape s;
        Type* t = site ? site->expr_type.type : nullptr;
        if (!t || !t->is_builtin()) return s;
        auto* b = static_cast<BuiltinType*>(t);
        if (b->base() != BK::Int && b->base() != BK::Char) return s;
        s.bits      = bit_width_of_rank(b->width());
        s.is_signed = !b->is_unsigned();
        return s;
    }

    IntShape shape_for(nodes::BinaryExpression* b) const {
        const bool shift = (b->op == TK::DoubleLessThan || b->op == TK::DoubleGreaterThan);
        return shape_of(shift ? b->left : static_cast<nodes::ASTNode*>(b));
    }

    static WideUInt width_mask(unsigned bits) {
        return bits >= unsigned(WideUInt::bits) ? ~WideUInt() : (WideUInt(std::uint64_t(1)) << bits) - WideUInt(std::uint64_t(1));
    }

    static WideUInt to_twos(const WideInt& v, unsigned bits) {
        return WideUInt(v) & width_mask(bits);          
    }

    static WideInt from_twos(const WideUInt& pattern, unsigned bits, bool is_signed) {
        const WideUInt m = width_mask(bits);
        const WideUInt p = pattern & m;
        
        if (is_signed && bits > 0 && p.get_bit(bits - 1)) {
            const WideUInt magnitude = ((~p) + WideUInt(std::uint64_t(1))) & m;
            return -WideInt(magnitude);
        }

        return WideInt(p);
    }

private:
    using K  = nodes::ASTNode::Kind;
    using TK = tokenizing::Token::Kind;

    TypeContext& m_types;
    std::unordered_set<const Symbol*> m_visiting;   
    int m_depth = 0;
    static constexpr int kMaxDepth = 512;

    ConstValue make_int(const WideInt& v) {
        if (v.is_undefined()) return {};
        ConstValue c; c.kind = ConstValue::Kind::Int; c.i = m_types.consts().intern(v);
        return c;
    }

    static ConstValue make_int(const WideInt* v) {
        ConstValue c; if (v) { c.kind = ConstValue::Kind::Int; c.i = v; }
        return c;
    }

    ConstValue make_float(const WideFloat& v) {
        if (v.is_undefined()) return {};
        ConstValue c; c.kind = ConstValue::Kind::Float; c.f = m_types.floats().intern(v);
        return c;
    }

    static ConstValue make_float(const WideFloat* v) {
        ConstValue c; if (v) { c.kind = ConstValue::Kind::Float; c.f = v; }
        return c;
    }

    static ConstValue make_bool(bool b) {
        ConstValue c; c.kind = ConstValue::Kind::Bool; c.b = b;
        return c;
    }

    ConstValue eval_impl(nodes::ASTNode* e) {
        switch (e->kind) {
            case K::Literal:             return eval_literal(static_cast<nodes::Literal*>(e));

            case K::TypeQuery: {
                auto* q = static_cast<nodes::TypeQueryExpression*>(e);
                using QOp = nodes::TypeQueryExpression::Op;
                if (q->pack_count >= 0) return make_int(WideInt(std::uint64_t(q->pack_count)));
                Type* t = q->queried ? m_types.strip_cv(m_types.apply_subst(q->queried)) : nullptr;
                if (!t || t->is_dependent()) return {};

                if (q->op == QOp::Countof) {
                    if (t->is_array() && static_cast<ArrayType*>(t)->extent()) return make_int(WideInt(std::uint64_t(*static_cast<ArrayType*>(t)->extent())));
                    return {};
                }

                long long bytes = 0, alignment = 0;
                if (!layout_of(t, bytes, alignment)) return {};
                if (q->op == QOp::Sizeof) return make_int(WideInt(std::uint64_t(bytes)));
                if (q->op == QOp::Alignof) return make_int(WideInt(std::uint64_t(alignment)));
                return {};
            }

            case K::FoldExpression: {
                auto* f = static_cast<nodes::FoldExpression*>(e);
                return f->lowered ? eval(f->lowered) : ConstValue{};
            }

            case K::TemplateInstantiation: {
                if (!concept_value) return {};
                const int r = concept_value(static_cast<nodes::TemplateInstantiation*>(e));
                return r < 0 ? ConstValue{} : make_bool(r == 1);
            }
            case K::Identifier:          return eval_symbol(static_cast<nodes::Identifier*>(e)->resolved);
            case K::QualifiedIdentifier: return eval_symbol(static_cast<nodes::QualifiedIdentifier*>(e)->resolved);

            case K::MemberAccessExpression: {   
                auto* m = static_cast<nodes::MemberAccessExpression*>(e);
                return m->resolved ? eval_symbol(m->resolved) : ConstValue{};
            }

            case K::CallExpression: return eval_call(static_cast<nodes::CallExpression*>(e));

            case K::UnaryExpression: {
                auto* u = static_cast<nodes::UnaryExpression*>(e);
                if (u->op == TK::DoublePlus || u->op == TK::DoubleMinus) return m_frames.empty() ? ConstValue{} : eval_incdec(u);
                ConstValue v = eval(u->operand);
                if (!v.ok()) return {};

                switch (u->op) {
                    case TK::Minus:
                        if (v.is_int())   return make_int(-*v.i);
                        if (v.is_float()) return make_float(-*v.f);
                        return {};
                    case TK::Plus:
                        return (v.is_int() || v.is_float()) ? v : ConstValue{};
                    case TK::ExclamationMark: {
                        bool ok = false; bool t = truthy(v, ok);
                        return ok ? make_bool(!t) : ConstValue{};
                    }
                    default: return {};
                }
            }

            case K::BitwiseNotExpression: {
                auto* n = static_cast<nodes::BitwiseNotExpression*>(e);
                ConstValue v = eval(n->operand);
                if (!v.is_int()) return {};
                Type* t = n->operand->expr_type.type;
                if (!t || !t->is_builtin()) return {};
                auto* b = static_cast<BuiltinType*>(t);
                if (b->base() != parser_types::PrimitiveType::BaseKind::Int) return {};
                return bitnot_at_width(*v.i, bit_width_of_rank(b->width()), !b->is_unsigned());
            }

            case K::BinaryExpression: return eval_binary(static_cast<nodes::BinaryExpression*>(e));

            case K::TernaryExpression: {
                auto* t = static_cast<nodes::TernaryExpression*>(e);
                ConstValue c = eval(t->condition);
                bool ok = false; bool cond = truthy(c, ok);
                if (!ok) return {};
                return eval(cond ? t->true_branch : t->false_branch);
            }

            case K::CastExpression: {
                auto* c = static_cast<nodes::CastExpression*>(e);
                ConstValue v = eval(c->operand);
                if (!v.ok()) return {};
                Type* dst = m_types.strip_cv(m_types.canonicalize(c->get_target()));
                if (!dst || !dst->is_builtin()) return {};
                return convert_to(v, static_cast<BuiltinType*>(dst));
            }

            case K::ThrowExpression: return ConstValue::error(ConstValue::Fail::Threw);

            case K::NoexceptExpression: {
                auto* n = static_cast<nodes::NoexceptExpression*>(e);
                if (!n->computed) return {};       
                return make_bool(n->is_nothrow);
            }

            default: return {};
        }
    }

    ConstValue eval_literal(nodes::Literal* lit) {
        switch (lit->get_kind()) {
            case TK::Integer: {
                std::string s(lit->value);
                if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) return make_int(m_types.consts().parse(s.substr(2), 16));
                if (s.size() > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) return make_int(m_types.consts().parse(s.substr(2), 2));
                return make_int(m_types.consts().parse(s, 10));
            }

            case TK::Float: return make_float(m_types.floats().parse(std::string(lit->value)));
            case TK::InfinityKeyword: return make_float(m_types.floats().intern(WideFloat::positive_infinity()));

            case TK::Character: {
                std::uint32_t cp = 0;
                tokenizing::EscapeError err = tokenizing::EscapeError::None;
                if (!tokenizing::decode_char(lit->value, cp, err)) return {};
                return make_int(WideInt(std::uint64_t(cp)));
            }

            case TK::True:  return make_bool(true);
            case TK::False: return make_bool(false);
            default: return {};
        }
    }

    ConstValue eval_symbol(Symbol* s) {
        if (!s) return {};

        if (!m_frames.empty()) {
            auto& locals = m_frames.back().locals;
            if (auto it = locals.find(s); it != locals.end()) return it->second.value;
        }

        switch (s->kind) {
            case SymbolKind::TemplateParam: {
                const auto& stack = m_types.subst_stack();
                for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
                    if (const WideInt*   v = (*it)->lookup_value(s))  return make_int(v);
                    if (const WideFloat* f = (*it)->lookup_fvalue(s)) return make_float(f);
                }
                return {};
            }

            case SymbolKind::EnumConstant: return eval_enum_constant(s);

            case SymbolKind::Variable: {
                if (!s->decl || s->decl->kind != K::VariableDeclaration) return {};
                auto* vd = static_cast<nodes::VariableDeclaration*>(s->decl);
                using RM = modifiers::RawModifiers;
                const auto& mods = vd->get_type_info().modifiers;
                if (!mods.has(RM::Constexpr) && !mods.has(RM::Const)) return {};
                if (!vd->has_initializer()) return {};
                const SubstEnv* env = env_of ? env_of(s) : nullptr;
                const bool cacheable = env != nullptr || m_types.subst_stack().empty();
                if (cacheable) if (auto it = m_symbol_cache.find(s); it != m_symbol_cache.end()) return it->second;
                if (!m_visiting.insert(s).second) return {};          
                if (env) m_types.push_subst(env);
                ConstValue v = coerce(eval(vd->m_initializer), m_types.canonicalize(vd->get_type_info()));
                if (env) m_types.pop_subst();
                m_visiting.erase(s);
                if (cacheable && v.ok()) m_symbol_cache.emplace(s, v);
                return v;
            }

            default: return {};
        }
    }

    struct Local {
        ConstValue value;
        Type*      type = nullptr;
    };

    struct Frame {
        std::unordered_map<const Symbol*, Local> locals;
        ConstValue                               result;
    };

    enum class Flow : std::uint8_t { Normal, Return, Break, Continue, Fail };

    std::vector<Frame> m_frames;
    std::unordered_map<const Symbol*, ConstValue> m_symbol_cache;
    ConstValue         m_failure;
    std::size_t        m_steps = 0;
    static constexpr std::size_t kMaxSteps = 1000000;
    static constexpr std::size_t kMaxCallDepth = 256;

    static TK compound_base(TK op) {
        switch (op) {
            case TK::PlusEqual:           return TK::Plus;
            case TK::MinusEqual:          return TK::Minus;
            case TK::AsteriskEqual:       return TK::Asterisk;
            case TK::SlashEqual:          return TK::Slash;
            case TK::PercentEqual:        return TK::Percent;
            case TK::DoubleAsteriskEqual: return TK::DoubleAsterisk;
            case TK::AmpersandEqual:      return TK::Ampersand;
            case TK::PipeEqual:           return TK::Pipe;
            case TK::CaretEqual:          return TK::Caret;
            case TK::ShiftLeftEqual:      return TK::DoubleLessThan;
            case TK::ShiftRightEqual:     return TK::DoubleGreaterThan;
            default:                      return op;
        }
    }

    static bool is_constexpr_function(const nodes::FunctionDeclaration* fd) {
        return fd->get_modifiers().has(modifiers::RawModifiers::Constexpr)
            || fd->qualifiers().has(modifiers::FunctionQualifiers::Consteval);
    }

    Local* local_of(nodes::ASTNode* target) {
        if (m_frames.empty() || !target) return nullptr;
        Symbol* s = nullptr;
        if      (target->kind == K::Identifier)          s = static_cast<nodes::Identifier*>(target)->resolved;
        else if (target->kind == K::QualifiedIdentifier) s = static_cast<nodes::QualifiedIdentifier*>(target)->resolved;
        if (!s) return nullptr;
        auto& locals = m_frames.back().locals;
        auto it = locals.find(s);
        return it == locals.end() ? nullptr : &it->second;
    }

    ConstValue store(Local& slot, ConstValue v) {
        if (!v.ok()) return v;
        ConstValue fitted = slot.type ? coerce(v, slot.type) : v;
        if (fitted.ok()) slot.value = fitted;
        return fitted;
    }

    ConstValue eval_assign(nodes::BinaryExpression* b) {
        Local* slot = local_of(b->left);
        if (!slot) return {};
        ConstValue r = eval(b->right);
        if (!r.ok()) return r;
        if (b->op == TK::Equal) return store(*slot, r);
        if (!slot->value.ok()) return {};
        return store(*slot, apply_binary(compound_base(b->op), slot->value, r, b));
    }

    ConstValue eval_incdec(nodes::UnaryExpression* u) {
        Local* slot = local_of(u->operand);
        if (!slot || !slot->value.ok()) return {};
        ConstValue before = slot->value;
        ConstValue one;
        if      (before.is_int())   one = make_int(WideInt(std::uint64_t(1)));
        else if (before.is_float()) one = make_float(WideFloat(1));
        else return {};
        nodes::BinaryExpression site(u->operand, u->op == TK::DoublePlus ? TK::Plus : TK::Minus, u->operand, u->line);
        site.expr_type = u->operand->expr_type;
        ConstValue after = store(*slot, apply_binary(site.op, before, one, &site));
        if (!after.ok()) return after;
        return u->is_prefix() ? after : before;
    }

    ConstValue eval_call(nodes::CallExpression* c) {
        Symbol* fn = c->resolved;
        if (!fn || fn->kind != SymbolKind::Function || !fn->decl || fn->decl->kind != K::FunctionDeclaration) return {};
        auto* fd = static_cast<nodes::FunctionDeclaration*>(fn->decl);
        if (!is_constexpr_function(fd) || !fd->has_body()) return {};
        if (m_frames.size() >= kMaxCallDepth) return ConstValue::error(ConstValue::Fail::StepLimit);

        const nodes::FunctionParameters* ps = fd->get_parameters();
        const std::size_t nparams = ps ? ps->m_params.size() : 0;
        if (c->m_arguments.size() > nparams) return ConstValue::error(ConstValue::Fail::BadCall);

        std::vector<ConstValue> args;
        args.reserve(nparams);

        for (std::size_t i = 0; i < nparams; ++i) {
            nodes::FunctionParameter* p = ps->m_params[i];
            if (p->is_variadic()) return {};
            nodes::ASTNode* src = i < c->m_arguments.size() ? c->m_arguments[i] : const_cast<nodes::ASTNode*>(p->get_initializer());
            if (!src) return ConstValue::error(ConstValue::Fail::BadCall);
            ConstValue v = eval(src);
            if (!v.ok()) return v;
            args.push_back(v);
        }

        const SubstEnv* env = env_of ? env_of(fn) : nullptr;
        if (env) m_types.push_subst(env);
        Frame frame;

        for (std::size_t i = 0; i < nparams; ++i) {
            nodes::FunctionParameter* p = ps->m_params[i];
            Local slot;
            slot.type = m_types.canonicalize(p->get_type());
            if (slot.type && slot.type->is_reference()) slot.type = static_cast<ReferenceType*>(slot.type)->referent();
            slot.value = coerce(args[i], slot.type);
            if (!slot.value.ok()) { if (env) m_types.pop_subst(); return slot.value.is_error() ? slot.value : ConstValue{}; }
            if (p->symbol) frame.locals[p->symbol] = slot;
        }

        Type* ret = m_types.canonicalize(fd->get_return_type());
        m_frames.push_back(std::move(frame));
        const Flow flow = exec(fd->get_body());
        ConstValue result = m_frames.back().result;
        m_frames.pop_back();
        if (env) m_types.pop_subst();

        if (flow == Flow::Fail)   return m_failure;
        if (flow != Flow::Return) return ConstValue::error(ConstValue::Fail::NoReturn);
        if (ret && ret->is_reference()) ret = static_cast<ReferenceType*>(ret)->referent();
        return coerce(result, ret);
    }

    Flow fail(ConstValue v) {
        m_failure = v.is_error() ? v : ConstValue{};
        return Flow::Fail;
    }

    bool test(nodes::ASTNode* cond, bool& taken) {
        ConstValue v = eval(cond);
        bool ok = false;
        taken = truthy(v, ok);
        if (!ok) { m_failure = v.is_error() ? v : ConstValue{}; return false; }
        return true;
    }

    Flow exec_loop_body(nodes::ASTNode* body, bool& stop) {
        stop = false;
        const Flow f = exec(body);
        if (f == Flow::Return || f == Flow::Fail) { stop = true; return f; }
        if (f == Flow::Break) stop = true;
        return Flow::Normal;
    }

    Flow exec(nodes::ASTNode* s) {
        if (!s) return Flow::Normal;
        if (++m_steps > kMaxSteps) return fail(ConstValue::error(ConstValue::Fail::StepLimit));

        switch (s->kind) {
            case K::BlockStatement:
                for (nodes::ASTNode* st : static_cast<nodes::BlockStatement*>(s)->statements) {
                    const Flow f = exec(st);
                    if (f != Flow::Normal) return f;
                }
                return Flow::Normal;

            case K::ExpressionStatement: {
                ConstValue v = eval(static_cast<nodes::ExpressionStatement*>(s)->expr);
                return v.ok() ? Flow::Normal : fail(v);
            }

            case K::VariableDeclaration: {
                auto* vd = static_cast<nodes::VariableDeclaration*>(s);
                if (!vd->symbol || !vd->m_bindings.empty()) return fail({});
                Local slot;
                slot.type = m_types.canonicalize(vd->get_type_info());
                if (vd->m_initializer) {
                    ConstValue v = eval(vd->m_initializer);
                    if (!v.ok()) return fail(v);
                    slot.value = coerce(v, slot.type);
                    if (!slot.value.ok()) return fail(slot.value);
                }
                m_frames.back().locals[vd->symbol] = slot;
                return Flow::Normal;
            }

            case K::ReturnStatement: {
                auto* r = static_cast<nodes::ReturnStatement*>(s);
                if (!r->has_value()) return fail({});
                ConstValue v = eval(r->get_value());
                if (!v.ok()) return fail(v);
                m_frames.back().result = v;
                return Flow::Return;
            }

            case K::IfStatement: {
                auto* is = static_cast<nodes::IfStatement*>(s);

                for (nodes::IfBranch* br : is->branches) {
                    bool taken = false;

                    if (br->condition && br->condition->kind == K::VariableDeclaration) {
                        const Flow f = exec(br->condition);
                        if (f != Flow::Normal) return f;
                        auto* vd = static_cast<nodes::VariableDeclaration*>(br->condition);
                        ConstValue v = eval_symbol(vd->symbol);
                        bool ok = false;
                        taken = truthy(v, ok);
                        if (!ok) return fail(v);
                    } else if (!test(br->condition, taken)) {
                        return Flow::Fail;
                    }

                    if (taken) return exec(br->body);
                }

                return exec(is->else_branch);
            }

            case K::WhileStatement: {
                auto* w = static_cast<nodes::WhileStatement*>(s);

                for (;;) {
                    bool taken = false, stop = false;
                    if (!test(w->condition, taken)) return Flow::Fail;
                    if (!taken) return Flow::Normal;
                    const Flow f = exec_loop_body(w->body, stop);
                    if (stop) return f == Flow::Normal ? Flow::Normal : f;
                    if (++m_steps > kMaxSteps) return fail(ConstValue::error(ConstValue::Fail::StepLimit));
                }
            }

            case K::DoWhileStatement: {
                auto* w = static_cast<nodes::DoWhileStatement*>(s);

                for (;;) {
                    bool taken = false, stop = false;
                    const Flow f = exec_loop_body(w->body, stop);
                    if (stop) return f == Flow::Normal ? Flow::Normal : f;
                    if (!test(w->condition, taken)) return Flow::Fail;
                    if (!taken) return Flow::Normal;
                    if (++m_steps > kMaxSteps) return fail(ConstValue::error(ConstValue::Fail::StepLimit));
                }
            }

            case K::ForStatement: {
                auto* fs = static_cast<nodes::ForStatement*>(s);

                if (fs->has_var_init) {
                    const Flow f = exec(fs->var_init);
                    if (f != Flow::Normal) return f;
                } else if (fs->initializer) {
                    ConstValue v = eval(fs->initializer);
                    if (!v.ok()) return fail(v);
                }

                for (;;) {
                    bool taken = true, stop = false;
                    if (fs->condition && !test(fs->condition, taken)) return Flow::Fail;
                    if (!taken) return Flow::Normal;
                    const Flow f = exec_loop_body(fs->body, stop);
                    if (stop) return f == Flow::Normal ? Flow::Normal : f;

                    if (fs->increment) {
                        ConstValue v = eval(fs->increment);
                        if (!v.ok()) return fail(v);
                    }

                    if (++m_steps > kMaxSteps) return fail(ConstValue::error(ConstValue::Fail::StepLimit));
                }
            }

            case K::SingleStatement: {
                using V = nodes::SingleStatement::Variant;
                switch (static_cast<nodes::SingleStatement*>(s)->variant) {
                    case V::Break:    return Flow::Break;
                    case V::Continue: return Flow::Continue;
                    default:          return fail({});
                }
            }

            case K::StaticAssertDeclaration: return Flow::Normal;
            default:                          return fail({});
        }
    }

    ConstValue eval_enum_constant(Symbol* s) {
        Symbol* en = s->owner ? s->owner->owner_symbol : nullptr;
        if (!en || !en->decl || en->decl->kind != K::EnumDeclaration) return {};
        auto* ed = static_cast<nodes::EnumDeclaration*>(en->decl);
        WideInt cur(std::uint64_t(0));

        for (nodes::EnumValue* v : ed->get_values()) {
            if (v->initializer) {
                ConstValue cv = eval(v->initializer);
                if (!cv.is_int()) return {};
                cur = *cv.i;
            }

            if (v->symbol == s) return make_int(cur);
            cur = cur + WideInt(std::uint64_t(1));
        }

        return {};
    }

    ConstValue eval_binary(nodes::BinaryExpression* b) {
        if (is_assignment_op(b->op)) return m_frames.empty() ? ConstValue{} : eval_assign(b);

        if (b->op == TK::LogicAnd || b->op == TK::LogicOr) {
            ConstValue l = eval(b->left);
            bool ok = false; bool lt = truthy(l, ok);
            if (!ok) return l.is_error() ? l : ConstValue{};
            if (b->op == TK::LogicAnd && !lt) return make_bool(false);
            if (b->op == TK::LogicOr  &&  lt) return make_bool(true);   
            ConstValue r = eval(b->right);
            bool rt = truthy(r, ok);
            return ok ? make_bool(rt) : (r.is_error() ? r : ConstValue{});
        }

        ConstValue l = eval(b->left), r = eval(b->right);
        if (!l.ok()) return l;                         
        if (!r.ok()) return r;
        return apply_binary(b->op, l, r, b);
    }

    ConstValue apply_binary(TK op, ConstValue l, ConstValue r, nodes::BinaryExpression* b) {
        if (!l.ok()) return l;
        if (!r.ok()) return r;

        if (l.is_float() || r.is_float()) {
            bool lossy = false;

            if (l.is_int()) {
                WideFloat w;
                lossy |= !wf_from_int_exact(*l.i, w);
                l = make_float(w);
            }

            if (r.is_int()) {
                WideFloat w;
                lossy |= !wf_from_int_exact(*r.i, w);
                r = make_float(w);
            }

            if (!l.is_float() || !r.is_float()) return {};
            ConstValue v = narrow(eval_float_op(op, *l.f, *r.f), b);
            if (v.ok() && lossy && v.fail == ConstValue::Fail::NotConstant) v.fail = ConstValue::Fail::Precision;
            return v;
        }

        if (l.is_int() && r.is_int()) return narrow(eval_int_op(op, *l.i, *r.i, shape_for(b)), b);

        if (l.is_bool() && r.is_bool()) {
            if (op == TK::LogicEqual) return make_bool(l.b == r.b);
            if (op == TK::NotEqual)   return make_bool(l.b != r.b);
        }

        return {};
    }

    ConstValue eval_int_op(TK op, const WideInt& a, const WideInt& b, const IntShape& sh) {
        switch (op) {
            case TK::Plus: {
                const WideInt r = a + b;
                return add_wrapped(a, b, r) ? ConstValue::error(ConstValue::Fail::Overflow) : make_int(r);
            }

            case TK::Minus: {
                const WideInt nb = -b, r = a + nb;
                return add_wrapped(a, nb, r) ? ConstValue::error(ConstValue::Fail::Overflow) : make_int(r);
            }

            case TK::Asterisk: {
                const WideInt r = a * b;
                return mul_wrapped(a, b, r) ? ConstValue::error(ConstValue::Fail::Overflow) : make_int(r);
            }

            case TK::Slash:
                if (b.is_zero()) return ConstValue::error(a.is_zero() ? ConstValue::Fail::Indeterminate : ConstValue::Fail::Undefined);
                return make_int(a / b);

            case TK::Percent:
                if (b.is_zero()) return ConstValue::error(a.is_zero() ? ConstValue::Fail::Indeterminate : ConstValue::Fail::Undefined);
                return make_int(a % b);

            case TK::DoubleAsterisk: return eval_int_pow(a, b);

            case TK::LogicEqual:   return make_bool(a == b);
            case TK::NotEqual:     return make_bool(a != b);
            case TK::LessThan:     return make_bool(a <  b);
            case TK::LessEqual:    return make_bool(a <= b);
            case TK::GreaterThan:  return make_bool(a >  b);
            case TK::GreaterEqual: return make_bool(a >= b);

            case TK::Ampersand:
            case TK::Pipe:
            case TK::Caret: {
                if (!sh.known()) return {};
                const WideUInt ua = to_twos(a, sh.bits), ub = to_twos(b, sh.bits);
                const WideUInt r = (op == TK::Ampersand) ? (ua & ub)
                                 : (op == TK::Pipe)      ? (ua | ub)
                                                         : (ua ^ ub);
                return make_int(from_twos(r, sh.bits, sh.is_signed));
            }

            case TK::DoubleLessThan:
            case TK::DoubleGreaterThan: {
                if (!sh.known())    return {};
                if (b.is_negative()) return ConstValue::error(ConstValue::Fail::ShiftWidth);
                if (b >= WideInt(std::uint64_t(sh.bits))) return ConstValue::error(ConstValue::Fail::ShiftWidth);
                const std::size_t n  = std::size_t(b.get_lowest_bits());
                const WideInt     p2 = WideInt(WideUInt(std::uint64_t(1)) << n);
                if (op == TK::DoubleGreaterThan) return make_int(a.floor_div(p2));  
                const WideInt r = a * p2;
                if (mul_wrapped(a, p2, r)) return ConstValue::error(ConstValue::Fail::Overflow);
                return make_int(r);                                                 
            }

            default: return {};
        }
    }

    ConstValue eval_int_pow(const WideInt& a, const WideInt& b) {
        if (a.is_undefined() || b.is_undefined()) return {};
        if (b.is_zero()) return make_int(WideInt(1));
        const long long hb = a.highest_bit();        
        const bool odd = b.get_bit(0);

        if (b.is_negative()) {
            if (hb <  0) return ConstValue::error(ConstValue::Fail::Undefined);        
            if (hb != 0) return ConstValue::error(ConstValue::Fail::Domain);             
            return make_int((a.is_negative() && odd) ? WideInt(-1) : WideInt(1));
        }

        if (hb <  0) return make_int(WideInt());
        if (hb == 0) return make_int((a.is_negative() && odd) ? WideInt(-1) : WideInt(1));
        if (b.highest_bit() >= 32) return ConstValue::error(ConstValue::Fail::Overflow);
        const std::uint64_t e = b.get_lowest_bits();
        if (static_cast<std::uint64_t>(hb) * e >= WideInt::bits) return ConstValue::error(ConstValue::Fail::Overflow);
        WideInt r(1), p(a);

        for (std::uint64_t t = e; t; t >>= 1) {
            if (t & 1ull) { if (!checked_mul(r, p)) return ConstValue::error(ConstValue::Fail::Overflow); }
            if (t >> 1)   { if (!checked_mul(p, p)) return ConstValue::error(ConstValue::Fail::Overflow); }
        }

        return make_int(r);
    }

    ConstValue guard_range(const WideFloat& r, const WideFloat& a, const WideFloat& b) {
        if (r.is_undefined()) return ConstValue::error(ConstValue::Fail::Indeterminate);
        if (r.is_nan())       return ConstValue::error(ConstValue::Fail::Domain);
        const bool finite_in = a.is_finite() && b.is_finite();
        if (finite_in && r.is_infinite()) return ConstValue::error(ConstValue::Fail::Overflow);
        if (finite_in && r.is_zero() && !a.is_zero() && !b.is_zero()) return ConstValue::error(ConstValue::Fail::Underflow);
        return make_float(r);
    }

    ConstValue eval_float_op(TK op, const WideFloat& a, const WideFloat& b) {
        switch (op) {
            case TK::Plus:
                if (a.is_infinite() && b.is_infinite() && a.is_negative() != b.is_negative())
                    return ConstValue::error(ConstValue::Fail::Indeterminate);         
                return guard_range(a + b, a, b);

            case TK::Minus:
                if (a.is_infinite() && b.is_infinite() && a.is_negative() == b.is_negative())
                    return ConstValue::error(ConstValue::Fail::Indeterminate);          
                return guard_range(a - b, a, b);

            case TK::Asterisk:
                if ((a.is_zero() && b.is_infinite()) || (a.is_infinite() && b.is_zero()))
                    return ConstValue::error(ConstValue::Fail::Indeterminate);         
                return guard_range(a * b, a, b);

            case TK::Slash:
                if (a.is_zero()     && b.is_zero())     return ConstValue::error(ConstValue::Fail::Indeterminate); 
                if (a.is_infinite() && b.is_infinite()) return ConstValue::error(ConstValue::Fail::Indeterminate);  
                if (b.is_zero())                        return ConstValue::error(ConstValue::Fail::Undefined);     
                return guard_range(a / b, a, b);

            case TK::DoubleAsterisk: return eval_float_pow(a, b);

            case TK::LogicEqual:   return make_bool(a == b);
            case TK::NotEqual:     return make_bool(a != b);
            case TK::LessThan:     return make_bool(a <  b);
            case TK::LessEqual:    return make_bool(a <= b);
            case TK::GreaterThan:  return make_bool(a >  b);
            case TK::GreaterEqual: return make_bool(a >= b);
            default: return {};
        }
    }

    static bool wf_is_integral(const WideFloat& x) {
        return x.is_finite() && x.get_fractional_part().is_zero();
    }

    ConstValue eval_float_pow(const WideFloat& a, const WideFloat& b) {
        const WideFloat one(1);
        const WideFloat mag = mp::math::abs(a);
        if (a.is_zero()     && b.is_zero())     return ConstValue::error(ConstValue::Fail::Indeterminate);  
        if (a.is_infinite() && b.is_zero())     return ConstValue::error(ConstValue::Fail::Indeterminate);  
        if (mag == one      && b.is_infinite()) return ConstValue::error(ConstValue::Fail::Indeterminate);  
        if (a.is_zero()     && b.is_negative()) return ConstValue::error(ConstValue::Fail::Undefined);
        if (a.is_negative() && b.is_finite() && !wf_is_integral(b)) return ConstValue::error(ConstValue::Fail::Domain);
        return guard_range(mp::math::pow(a, b), a, b);
    }

    ConstValue finite_result(const WideFloat& r, const WideFloat& a, const WideFloat& b) {
        const bool inputs_finite = a.is_finite() && b.is_finite();
        if (r.is_undefined()) return ConstValue::error(ConstValue::Fail::Domain);
        if (inputs_finite && r.is_nan())      return ConstValue::error(ConstValue::Fail::Domain);
        if (inputs_finite && r.is_infinite()) return ConstValue::error(ConstValue::Fail::Overflow);
        if (inputs_finite && r.is_zero() && !a.is_zero() && !b.is_zero()) return ConstValue::error(ConstValue::Fail::Underflow);
        return make_float(r);
    }

    ConstValue convert_to(const ConstValue& v, BuiltinType* dst) {
        using BK = parser_types::PrimitiveType::BaseKind;

        switch (dst->base()) {
            case BK::Bool: {
                bool ok = false; bool t = truthy(v, ok);
                return ok ? make_bool(t) : ConstValue{};
            }

            case BK::Int: case BK::Char: {
                WideInt w;
                if      (v.is_int())   w = *v.i;
                else if (v.is_float()) { if (!wf_trunc_to_int(*v.f, w)) return {}; }
                else if (v.is_bool())  w = WideInt(std::uint64_t(v.b ? 1 : 0));
                else return {};
                
                if (dst->base() == BK::Char) return char_fits(w) ? make_int(w) : ConstValue{};
                if (!ConstTable::fits(w, bit_width_of_rank(dst->width()), !dst->is_unsigned())) return {};
                return make_int(w);
            }

            case BK::Float: {
                WideFloat src;

                if      (v.is_float()) src = *v.f;
                else if (v.is_int())   src = wf_from_int(*v.i);
                else return {};

                WideFloat out;
                if (!wf_round_to_width(src, bit_width_of_rank(dst->width()), out)) return {};
                if (!src.is_infinite() && out.is_infinite()) return ConstValue::error(ConstValue::Fail::Overflow);
                if (!src.is_zero()     && out.is_zero())     return ConstValue::error(ConstValue::Fail::Underflow);
                return make_float(out);
            }

            default: return {};
        }
    }

    ConstValue bitnot_at_width(const WideInt& v, unsigned width, bool is_signed) {
        if (v.is_undefined() || width == 0) return {};
        if (!ConstTable::fits(v, width, is_signed)) return {};
        WideUInt bits(v);
        WideUInt mask = width >= unsigned(WideInt::bits) ? ~WideUInt() : (WideUInt(std::uint64_t(1)) << width) - WideUInt(std::uint64_t(1));
        WideUInt flipped = (~bits) & mask;

        if (is_signed && flipped.get_bit(width - 1)) {
            WideUInt mag = ((~flipped) + WideUInt(std::uint64_t(1))) & mask;
            return make_int(-WideInt(mag));
        }
        
        return make_int(WideInt(flipped));
    }
};

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMANTICS_CONSTANT_EVALUATOR_HPP