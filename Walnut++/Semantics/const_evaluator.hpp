#ifndef WALNUT_SEMANTICS_CONSTANT_EVALUATOR_HPP
#define WALNUT_SEMANTICS_CONSTANT_EVALUATOR_HPP

#include "type_impl.hpp"
#include "substitution.hpp"
#include "const_value.hpp"
#include "symbol.hpp"
#include "scope.hpp"

#include "../Parser/nodes.hpp"

#include <unordered_set>

namespace walnut {
namespace semantics {

struct ConstValue {
    enum class Kind : std::uint8_t { None = 0, Int, Float, Bool } kind = Kind::None;
    const WideInt*   i = nullptr;
    const WideFloat* f = nullptr;
    bool             b = false;

    bool ok()       const { return kind != Kind::None; }
    bool is_int()   const { return kind == Kind::Int; }
    bool is_float() const { return kind == Kind::Float; }
    bool is_bool()  const { return kind == Kind::Bool; }
};

class ConstEvaluator {
public:
    explicit ConstEvaluator(TypeContext& types) : m_types(types) {}

    ConstValue eval(nodes::ASTNode* e) {
        if (!e || m_depth > kMaxDepth) return {};
        ++m_depth;
        ConstValue v = eval_impl(e);
        --m_depth;
        return v;
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
            case K::Identifier:          return eval_symbol(static_cast<nodes::Identifier*>(e)->resolved);
            case K::QualifiedIdentifier: return eval_symbol(static_cast<nodes::QualifiedIdentifier*>(e)->resolved);

            case K::MemberAccessExpression: {   
                auto* m = static_cast<nodes::MemberAccessExpression*>(e);
                return m->resolved ? eval_symbol(m->resolved) : ConstValue{};
            }

            case K::UnaryExpression: {
                auto* u = static_cast<nodes::UnaryExpression*>(e);
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

            case K::NoexceptExpression: return make_bool(true);   // until exceptions reach the checker

            default: return {};
        }
    }

    ConstValue eval_literal(nodes::Literal* lit) {
        switch (lit->get_kind()) {
            case TK::Integer: {
                std::string s(lit->value);
                if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) return make_int(m_types.consts().parse(s, 16));
                if (s.size() > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) return make_int(m_types.consts().parse(s.substr(2), 2));
                return make_int(m_types.consts().parse(s, 10));
            }

            case TK::Float: return make_float(m_types.floats().parse(std::string(lit->value)));
            case TK::InfinityKeyword: return make_float(m_types.floats().intern(WideFloat::positive_infinity()));

            case TK::Character: {
                // TODO(escapes): \n, \t, \xNN once the lexeme convention is settled
                std::string_view s = lit->value;
                if (s.size() >= 3 && s.front() == '\'' && s.back() == '\'') s = s.substr(1, s.size() - 2);
                if (s.size() != 1) return {};
                return make_int(WideInt(std::uint64_t(std::uint8_t(s[0]))));
            }

            case TK::True:  return make_bool(true);
            case TK::False: return make_bool(false);
            default: return {};
        }
    }

    ConstValue eval_symbol(Symbol* s) {
        if (!s) return {};

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
                if (!m_visiting.insert(s).second) return {};          
                ConstValue v = eval(vd->m_initializer);
                m_visiting.erase(s);
                return v;
            }

            default: return {};
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
        if (b->op == TK::LogicAnd || b->op == TK::LogicOr) {
            ConstValue l = eval(b->left);
            bool ok = false; bool lt = truthy(l, ok);
            if (!ok) return {};
            if (b->op == TK::LogicAnd && !lt) return make_bool(false);
            if (b->op == TK::LogicOr  &&  lt) return make_bool(true);
            ConstValue r = eval(b->right);
            bool rt = truthy(r, ok);
            return ok ? make_bool(rt) : ConstValue{};
        }

        ConstValue l = eval(b->left), r = eval(b->right);
        if (!l.ok() || !r.ok()) return {};

        if (l.is_float() || r.is_float()) {
            if (l.is_int()) l = make_float(m_types.floats().from_int(*l.i));
            if (r.is_int()) r = make_float(m_types.floats().from_int(*r.i));
            if (!l.is_float() || !r.is_float()) return {};
            return eval_float_op(b->op, *l.f, *r.f);
        }

        if (l.is_int() && r.is_int()) return eval_int_op(b->op, *l.i, *r.i);

        if (l.is_bool() && r.is_bool()) {
            if (b->op == TK::LogicEqual) return make_bool(l.b == r.b);
            if (b->op == TK::NotEqual)   return make_bool(l.b != r.b);
        }

        return {};
    }

    ConstValue eval_int_op(TK op, const WideInt& a, const WideInt& b) {
        switch (op) {
            case TK::Plus:     return make_int(a + b);
            case TK::Minus:    return make_int(a - b);
            case TK::Asterisk: return make_int(a * b);
            case TK::Slash:    return make_int(a / b);   
            case TK::Percent:  return make_int(a % b);

            case TK::LogicEqual:   return make_bool(a == b);
            case TK::NotEqual:     return make_bool(a != b);
            case TK::LessThan:     return make_bool(a <  b);
            case TK::LessEqual:    return make_bool(a <= b);
            case TK::GreaterThan:  return make_bool(a >  b);
            case TK::GreaterEqual: return make_bool(a >= b);

          
            case TK::Ampersand: 
            case TK::Pipe: 
            case TK::Caret:
            case TK::DoubleLessThan: 
            case TK::DoubleGreaterThan: {
                if (a.is_negative() || b.is_negative() || a.is_undefined() || b.is_undefined()) return {};
                WideUInt ua(a), ub(b);

                switch (op) {
                    case TK::Ampersand: return make_int(WideInt(ua & ub));
                    case TK::Pipe:      return make_int(WideInt(ua | ub));
                    case TK::Caret:     return make_int(WideInt(ua ^ ub));
                    default: {
                        if (b >= WideInt(std::uint64_t(WideInt::bits))) return {};
                        std::size_t sh = std::size_t(b.get_lowest_bits());
                        return make_int(WideInt(op == TK::DoubleLessThan ? ua << sh : ua >> sh));
                    }
                }
            }

            default: return {};
        }
    }

    ConstValue eval_float_op(TK op, const WideFloat& a, const WideFloat& b) {
        switch (op) {
            case TK::Plus:         return make_float(a + b);   
            case TK::Minus:        return make_float(a - b);
            case TK::Asterisk:     return make_float(a * b);   
            case TK::Slash:        return make_float(a / b);  
            case TK::LogicEqual:   return make_bool(a == b);
            case TK::NotEqual:     return make_bool(a != b);
            case TK::LessThan:     return make_bool(a <  b);
            case TK::LessEqual:    return make_bool(a <= b);
            case TK::GreaterThan:  return make_bool(a >  b);
            case TK::GreaterEqual: return make_bool(a >= b);
            default: return {};
        }
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
                
                if (!ConstTable::fits(w, bit_width_of_rank(dst->width()), !dst->is_unsigned())) return {};
                return make_int(w);
            }

            case BK::Float: {
                if (v.is_float()) return v;   // TODO(dispatch-spine): round to dst's float width
                if (v.is_int())   return make_float(m_types.floats().from_int(*v.i));
                return {};
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