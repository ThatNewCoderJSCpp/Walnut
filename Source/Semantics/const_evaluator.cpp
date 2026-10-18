#include "Semantics/const_evaluator.hpp"
#include "Semantics/type_ops.hpp"
#include "Lexer/escapes.hpp"
#include "Lexer/numeric_literal.hpp"

namespace walnut {
namespace semantics {

const char* fail_message(ConstValue::Fail f) {
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

const char* special_spelling(SpecialKind k) {
    switch (k) {
        case SpecialKind::PosInf:        return "inf";
        case SpecialKind::NegInf:        return "-inf";
        case SpecialKind::Nan:           return "nan";
        case SpecialKind::Undefined:     return "undefined";
        case SpecialKind::Indeterminate: return "indeterminate";
    }
    return "undefined";
}

WideFloat special_to_float(SpecialKind k) {
    switch (k) {
        case SpecialKind::PosInf:        return WideFloat::infinity(false);
        case SpecialKind::NegInf:        return WideFloat::infinity(true);
        case SpecialKind::Nan:           return WideFloat::nan();
        case SpecialKind::Undefined:     return WideFloat::undefined();
        case SpecialKind::Indeterminate: return WideFloat::indeterminate();
    }
    return WideFloat::undefined();
}

static ConstValue::Fail warning_for(SpecialKind k) {
    switch (k) {
        case SpecialKind::Undefined:     return ConstValue::Fail::Undefined;
        case SpecialKind::Indeterminate: return ConstValue::Fail::Indeterminate;
        case SpecialKind::Nan:           return ConstValue::Fail::Domain;
        default:                         return ConstValue::Fail::NotConstant;
    }
}

static bool unordered_kind(SpecialKind k) {
    return k == SpecialKind::Nan || k == SpecialKind::Undefined || k == SpecialKind::Indeterminate;
}

ConstValue ConstEvaluator::special_result(SpecialKind k, const ConstValue& l, const ConstValue& r) {
    const bool inherited = l.is_unordered() || r.is_unordered();
    return make_special(k, (unordered_kind(k) && !inherited) ? warning_for(k) : ConstValue::Fail::NotConstant);
}

ConstValue ConstEvaluator::special_of_float(const WideFloat& f, bool inputs_unordered) {
    SpecialKind k = SpecialKind::Undefined;
    if      (f.is_nan())           k = SpecialKind::Nan;
    else if (f.is_indeterminate()) k = SpecialKind::Indeterminate;
    else if (f.is_infinite())      k = f.is_negative() ? SpecialKind::NegInf : SpecialKind::PosInf;
    return make_special(k, (unordered_kind(k) && !inputs_unordered) ? warning_for(k) : ConstValue::Fail::NotConstant);
}

bool ConstEvaluator::as_wide_float(const ConstValue& v, WideFloat& out) {
    if (v.is_float())   { out = *v.f; return true; }
    if (v.is_int())     { out = wf_from_int(*v.i); return true; }
    if (v.is_special()) { out = special_to_float(v.sp); return true; }
    return false;
}

static SpecialKind unordered_of(const ConstValue& a, const ConstValue& b) {
    auto has = [&](SpecialKind k) { return (a.is_special() && a.sp == k) || (b.is_special() && b.sp == k); };
    if (has(SpecialKind::Undefined))     return SpecialKind::Undefined;
    if (has(SpecialKind::Indeterminate)) return SpecialKind::Indeterminate;
    return SpecialKind::Nan;
}

ConstValue ConstEvaluator::special_int_op(TK op, const ConstValue& l, const ConstValue& r) {
    auto neg = [](const ConstValue& v) { return v.is_special() ? v.sp == SpecialKind::NegInf : v.i->is_negative(); };
    auto zero = [](const ConstValue& v) { return v.is_int() && v.i->is_zero(); };
    auto inf = [&](bool n) { return special_result(n ? SpecialKind::NegInf : SpecialKind::PosInf, l, r); };
    auto sp = [&](SpecialKind k) { return special_result(k, l, r); };

    if (l.is_unordered() || r.is_unordered()) return sp(unordered_of(l, r));

    switch (op) {
        case TK::Plus:
            if (l.is_infinite() && r.is_infinite()) return l.sp == r.sp ? l : sp(SpecialKind::Indeterminate);
            return l.is_infinite() ? l : r;

        case TK::Minus:
            if (l.is_infinite() && r.is_infinite()) return l.sp == r.sp ? sp(SpecialKind::Indeterminate) : l;
            return l.is_infinite() ? l : inf(r.sp == SpecialKind::PosInf);

        case TK::Asterisk:
            if (zero(l) || zero(r)) return sp(SpecialKind::Indeterminate);
            return inf(neg(l) != neg(r));

        case TK::Slash:
            if (zero(r)) return sp(zero(l) ? SpecialKind::Indeterminate : SpecialKind::Undefined);
            if (l.is_infinite() && r.is_infinite()) return sp(SpecialKind::Indeterminate);
            if (l.is_infinite()) return inf(neg(l) != neg(r));
            return make_int(WideInt());

        case TK::Percent:
            if (zero(r)) return sp(zero(l) ? SpecialKind::Indeterminate : SpecialKind::Undefined);
            if (l.is_infinite()) return sp(SpecialKind::Undefined);
            return l;

        case TK::DoubleAsterisk: {
            const bool one = l.is_int() && *l.i == WideInt(1);
            const bool minus_one = l.is_int() && *l.i == WideInt(-1);
            if (zero(r)) return (zero(l) || l.is_infinite()) ? sp(SpecialKind::Indeterminate) : make_int(WideInt(1));
            if (one) return r.is_infinite() ? sp(SpecialKind::Indeterminate) : make_int(WideInt(1));

            if (r.is_infinite()) {
                if (zero(l)) return neg(r) ? sp(SpecialKind::Undefined) : make_int(WideInt());
                if (neg(r)) return make_int(WideInt());
                if (neg(l)) return minus_one ? sp(SpecialKind::Indeterminate) : sp(SpecialKind::Undefined);
                return inf(false);
            }

            if (neg(r)) return make_int(WideInt());
            return inf(neg(l) && r.i->get_bit(0));
        }

        case TK::Ampersand: case TK::Pipe: case TK::Caret:
        case TK::DoubleLessThan: case TK::DoubleGreaterThan:
            return sp(SpecialKind::Undefined);

        default: {
            WideFloat a, b;
            if (!as_wide_float(l, a) || !as_wide_float(r, b)) return {};
            switch (op) {
                case TK::LogicEqual:   return make_bool(a == b);
                case TK::NotEqual:     return make_bool(a != b);
                case TK::LessThan:     return make_bool(a <  b);
                case TK::LessEqual:    return make_bool(a <= b);
                case TK::GreaterThan:  return make_bool(a >  b);
                case TK::GreaterEqual: return make_bool(a >= b);
                default:               return {};
            }
        }
    }
}

ConstValue ConstEvaluator::apply_special(TK op, const ConstValue& l, const ConstValue& r, nodes::BinaryExpression* b) {
    using BK = parser_types::PrimitiveType::BaseKind;
    Type* site = b ? b->expr_type.type : nullptr;
    const bool int_site = site && site->is_builtin() && static_cast<BuiltinType*>(site)->base() == BK::Int;
    const bool compare = op == TK::LogicEqual || op == TK::NotEqual || op == TK::LessThan || op == TK::LessEqual || op == TK::GreaterThan || op == TK::GreaterEqual;

    if (int_site && !l.is_float() && !r.is_float() && !compare) return special_int_op(op, l, r);

    WideFloat a, c;
    if (!as_wide_float(l, a) || !as_wide_float(r, c)) return {};
    if (compare) return eval_float_op(op, a, c);
    if (op == TK::Percent || op == TK::Ampersand || op == TK::Pipe || op == TK::Caret || op == TK::DoubleLessThan || op == TK::DoubleGreaterThan) return special_int_op(op, l, r);
    return eval_float_op(op, a, c);
}

bool ConstEvaluator::is_constant_type(Type* t) {
    using BK = parser_types::PrimitiveType::BaseKind;
    if (!t || !t->is_builtin()) return false;
    switch (static_cast<BuiltinType*>(t)->base()) {
        case BK::Int: case BK::Float: case BK::Char: case BK::Bool: return true;
        default: return false;
    }
}

ConstValue ConstEvaluator::coerce(const ConstValue& v, Type* dst) {
    using BK = parser_types::PrimitiveType::BaseKind;
    if (!v.ok() || !dst) return v;
    dst = m_types.strip_cv(dst);
    if (!is_constant_type(dst)) return v;
    auto* b = static_cast<BuiltinType*>(dst);

    if (v.is_special()) {
        if (b->base() == BK::Bool) return make_bool(true);
        if (b->base() != BK::Int && b->base() != BK::Float) return {};
        if (v.sp == SpecialKind::NegInf && b->is_unsigned()) return ConstValue::error(ConstValue::Fail::Overflow);
        return v;
    }

    if (b->base() == BK::Float && b->is_unsigned()) {
        if ((v.is_int() && v.i->is_negative()) || (v.is_float() && v.f->is_negative() && !v.f->is_zero())) return ConstValue::error(ConstValue::Fail::Overflow);
    }
    ConstValue out = convert_to(v, b);
    if (out.ok() || out.is_error()) return out;
    if (v.is_int() && (b->base() == BK::Int || b->base() == BK::Char)) return ConstValue::error(ConstValue::Fail::Overflow);
    if (v.is_float() && (b->base() == BK::Int || b->base() == BK::Char)) return ConstValue::error(ConstValue::Fail::Overflow);
    return out;
}

bool ConstEvaluator::truthy(const ConstValue& v, bool& ok) {
    ok = true;
    switch (v.kind) {
        case ConstValue::Kind::Bool:  return v.b;
        case ConstValue::Kind::Int:   return !v.i->is_zero();
        case ConstValue::Kind::Float: return !v.f->is_zero();
        case ConstValue::Kind::Special: return true;
        default: ok = false; return false;
    }
}

bool ConstEvaluator::checked_mul(WideInt& acc, const WideInt& x) {
    if (acc.is_zero() || x.is_zero()) { acc = WideInt(); return true; }
    const WideInt r = acc * x;
    if (r / x != acc) return false;
    acc = r;
    return true;
}

bool ConstEvaluator::layout_of(Type* t, long long& size, long long& align, int depth) {
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

long long ConstEvaluator::alignment_of(const parser_types::TemplateArgument* a) {
    if (!a) return 0;
    if (a->align_value > 0) return a->align_value;

    const bool named_value = a->is_type() && a->type.resolved && (a->type.resolved->kind == SymbolKind::Variable || a->type.resolved->kind == SymbolKind::EnumConstant);

    if (a->is_type() && !named_value) {
        long long s = 0, al = 0;
        Type* t = m_types.canonicalize(const_cast<parser_types::TypeInfo&>(a->type));
        return t && layout_of(t, s, al) ? al : 0;
    }

    ConstValue v = named_value ? eval_symbol(a->type.resolved) : eval(a->value);
    if (!v.ok() || !v.is_int() || v.i->is_negative() || !ConstTable::fits(*v.i, 32, false)) return 0;
    return static_cast<long long>(v.i->get_lowest_bits());
}

bool ConstEvaluator::record_layout(RecordType* rt, long long& size, long long& align, int depth) {
    Symbol* owner = rt->decl();
    if (rt->is_instantiation() && m_types.record_scope_hook) {
        Scope* sc = m_types.record_scope_hook(rt);
        if (!sc || !sc->owner_symbol) return false;
        owner = sc->owner_symbol;
    }
    if (!owner || !owner->decl || owner->decl->kind != K::RecordDeclaration) return false;
    auto* rd = static_cast<nodes::RecordDeclaration*>(owner->decl);
    if (rd->m_native) { layout_hit_native = true; return false; }
    const SubstEnv* env = env_of ? env_of(owner) : nullptr;
    if (env) m_types.push_subst(env);
    struct Pop { TypeContext& t; bool on; ~Pop() { if (on) t.pop_subst(); } } pop{ m_types, env != nullptr };
    long long offset = 0;
    align = 1;
    bool has_vptr = false;

    for (parser_types::TypeInfo* info : rd->all_bases()) {
        long long bs = 0, ba = 0;
        if (!layout_of(m_types.canonicalize(*info), bs, ba, depth + 1)) return false;
        offset = (offset + ba - 1) / ba * ba + bs;
        if (ba > align) align = ba;
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

        long long forced = 0;

        if (n->kind == K::VariableDeclaration) {
            auto* v = static_cast<nodes::VariableDeclaration*>(n);
            if (v->get_type_info().modifiers.has(RM::Static)) continue;
            ft = m_types.canonicalize(v->get_type_info());
            forced = alignment_of(v->get_type_info().alignment);
        } else if (n->kind == K::ArrayDeclaration) {
            auto* a = static_cast<nodes::ArrayDeclaration*>(n);
            if (a->get_array_modifiers().has(RM::Static)) continue;
            forced = alignment_of(a->m_alignment);
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
        if (forced > fa) fa = forced;
        if (n->kind == K::VariableDeclaration && static_cast<nodes::VariableDeclaration*>(n)->get_type_info().modifiers.has(RM::Lazy)) fs = (fs + 1 + fa - 1) / fa * fa;
        if (rd->is_union()) offset = fs > offset ? fs : offset;
        else offset = (offset + fa - 1) / fa * fa + fs;
        if (fa > align) align = fa;
    }

    if (const long long forced = alignment_of(rd->m_alignment); forced > align) align = forced;
    size = (offset + align - 1) / align * align;
    if (size == 0) size = 1;
    return true;
}

bool ConstEvaluator::char_fits(const WideInt& v) {
    return !v.is_undefined() && !v.is_negative() && ConstTable::fits(v, 21, false) && !(WideInt(std::uint64_t(0x10FFFF)) < v);
}

ConstValue ConstEvaluator::narrow(ConstValue v, nodes::ASTNode* site) {
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

auto ConstEvaluator::shape_of(nodes::ASTNode* site) const -> IntShape {
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

auto ConstEvaluator::shape_for(nodes::BinaryExpression* b) const -> IntShape {
    const bool shift = (b->op == TK::DoubleLessThan || b->op == TK::DoubleGreaterThan);
    return shape_of(shift ? b->left : static_cast<nodes::ASTNode*>(b));
}

WideUInt ConstEvaluator::width_mask(unsigned bits) {
    return bits >= unsigned(WideUInt::bits) ? ~WideUInt() : (WideUInt(std::uint64_t(1)) << bits) - WideUInt(std::uint64_t(1));
}

WideInt ConstEvaluator::from_twos(const WideUInt& pattern, unsigned bits, bool is_signed) {
    const WideUInt m = width_mask(bits);
    const WideUInt p = pattern & m;
        
    if (is_signed && bits > 0 && p.get_bit(bits - 1)) {
        const WideUInt magnitude = ((~p) + WideUInt(std::uint64_t(1))) & m;
        return -WideInt(magnitude);
    }

    return WideInt(p);
}

ConstValue ConstEvaluator::make_int(const WideInt& v) {
    if (v.is_undefined()) return {};
    ConstValue c; c.kind = ConstValue::Kind::Int; c.i = m_types.consts().intern(v);
    return c;
}

ConstValue ConstEvaluator::make_float(const WideFloat& v) {
    if (!v.is_finite()) return special_of_float(v, true);
    ConstValue c; c.kind = ConstValue::Kind::Float; c.f = m_types.floats().intern(v);
    return c;
}

ConstValue ConstEvaluator::eval_impl(nodes::ASTNode* e) {
    switch (e->kind) {
        case K::Literal:             return eval_literal(static_cast<nodes::Literal*>(e));

        case K::TypeQuery: {
            auto* q = static_cast<nodes::TypeQueryExpression*>(e);
            using QOp = nodes::TypeQueryExpression::Op;

            if (q->op == QOp::Trait) {
                std::vector<Type*> types;

                if (trait_has_pack(q->extra_operands)) {
                    types = trait_operands(m_types, q->operand, q->extra_operands);
                } else if (q->queried) {
                    types.push_back(m_types.apply_subst(q->queried));
                    for (Type* x : q->extra_queried) types.push_back(x ? m_types.apply_subst(x) : nullptr);
                } else {
                    types.push_back(q->operand ? m_types.canonicalize(q->operand->type) : nullptr);
                    for (parser_types::TemplateArgument* x : q->extra_operands) types.push_back(x ? m_types.canonicalize(x->type) : nullptr);
                }
                std::string error;
                const std::optional<std::int64_t> r = eval_type_trait(m_types, q->trait, types, error);
                if (!error.empty() && m_types.type_error_hook) m_types.type_error_hook(error);
                if (!r) return ConstValue{};
                if (parser_types::type_trait_gives_int(q->trait)) return make_int(WideInt(*r));
                return make_bool(*r != 0);
            }

            if (q->pack_count >= 0) return make_int(WideInt(std::uint64_t(q->pack_count)));
            Type* t = q->queried ? m_types.strip_cv(m_types.apply_subst(q->queried)) : nullptr;
            if (!t && q->operand && q->operand->form == parser_types::TemplateArgument::Form::Type && q->operand->type.has_type()) {
                if (Type* c = m_types.canonicalize(q->operand->type)) t = m_types.strip_cv(c);
            }
            if (!t || t->is_dependent()) return {};

            if (q->op == QOp::Countof) {
                if (t->is_array() && static_cast<ArrayType*>(t)->extent()) return make_int(WideInt(std::uint64_t(*static_cast<ArrayType*>(t)->extent())));
                if (std::optional<std::size_t> n = enumerator_count(t)) return make_int(WideInt(std::uint64_t(*n)));
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
            if (variable_template_value) {
                ConstValue v = variable_template_value(static_cast<nodes::TemplateInstantiation*>(e));
                if (v.ok() || v.is_error()) return v;
            }
            if (!concept_value) return {};
            const int r = concept_value(static_cast<nodes::TemplateInstantiation*>(e));
            return r < 0 ? ConstValue{} : make_bool(r == 1);
        }
        case K::Identifier:          return eval_symbol(static_cast<nodes::Identifier*>(e)->resolved);
        case K::QualifiedIdentifier: return eval_symbol(static_cast<nodes::QualifiedIdentifier*>(e)->resolved);

        case K::MemberAccessExpression: {   
            auto* m = static_cast<nodes::MemberAccessExpression*>(e);
            if (scoped_member_value && m->is_scope() && m->m_object && m->m_object->kind == K::TemplateInstantiation) {
                ConstValue v = scoped_member_value(m);
                if (v.ok() || v.is_error()) return v;
            }
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
                    if (v.is_int()) {
                        const IntShape sh = shape_of(u);
                        if (sh.bits && !sh.is_signed) return make_int(from_twos(to_twos(-*v.i, sh.bits), sh.bits, false));
                        return make_int(-*v.i);
                    }
                    if (v.is_float()) return make_float(-*v.f);
                    if (v.is_special()) {
                        if (v.sp == SpecialKind::PosInf) return make_special(SpecialKind::NegInf);
                        if (v.sp == SpecialKind::NegInf) return make_special(SpecialKind::PosInf);
                        return v;
                    }
                    return {};
                case TK::Plus:
                    return (v.is_int() || v.is_float() || v.is_special()) ? v : ConstValue{};
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
            if (c->get_cast_kind() == nodes::CastExpression::CastKind::Is && c->special_test) {
                if (v.is_error() || v.kind == ConstValue::Kind::None) return v.is_error() ? v : ConstValue{};
                if (v.kind == ConstValue::Kind::Int || v.kind == ConstValue::Kind::Float) return make_bool(false);
                if (!v.is_special()) return {};
                switch (c->special_test) {
                    case 1: return make_bool(v.sp == SpecialKind::PosInf || v.sp == SpecialKind::NegInf);
                    case 2: return make_bool(v.sp == SpecialKind::Nan);
                    case 3: return make_bool(v.sp == SpecialKind::Undefined);
                    default: return make_bool(v.sp == SpecialKind::Indeterminate);
                }
            }
            if (c->get_cast_kind() == nodes::CastExpression::CastKind::Is || c->get_cast_kind() == nodes::CastExpression::CastKind::As) return {};
            if (!v.ok()) return v;
            if (Type* target = m_types.canonicalize(c->get_target()); target && target->is_reference()) return v;
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

auto ConstEvaluator::eval_literal(nodes::Literal* lit) -> ConstValue {
    switch (lit->get_kind()) {
        case TK::Integer: {
            const tokenizing::NumericLiteral n = tokenizing::parse_numeric_literal(lit->value);
            if (!n.error.empty() || n.digits.empty()) return {};
            const WideInt* v = m_types.consts().parse(n.digits, n.base);
            if (!v || !n.has_suffix) return make_int(v);
            const unsigned w = bit_width_of_rank(n.rank);

            if (n.type_is_float) {
                WideFloat r;
                const WideFloat f = wf_from_int(*v);
                return wf_round_to_width(f, w, r) ? make_float(r) : make_float(f);
            }

            return make_int(from_twos(to_twos(*v, w), w, !n.is_unsigned));
        }

        case TK::Float: {
            const tokenizing::NumericLiteral n = tokenizing::parse_numeric_literal(lit->value);
            if (!n.error.empty()) return {};
            const WideFloat* f = m_types.floats().parse(n.body);
            if (!f || !n.has_suffix) return make_float(f);
            WideFloat r;
            if (!wf_round_to_width(*f, bit_width_of_rank(n.rank), r)) return make_float(f);
            return make_float(r);
        }
        case TK::InfinityKeyword:      return make_special(SpecialKind::PosInf);
        case TK::NanKeyword:           return make_special(SpecialKind::Nan);
        case TK::UndefinedKeyword:     return make_special(SpecialKind::Undefined);
        case TK::IndeterminateKeyword: return make_special(SpecialKind::Indeterminate);

        case TK::Character: {
            std::uint32_t cp = 0;
            tokenizing::EscapeError err = tokenizing::EscapeError::None;
            if (!tokenizing::decode_char(lit->value, cp, err)) return {};
            return make_int(WideInt(std::uint64_t(cp)));
        }

        case TK::String:
        case TK::TextLiteral: {
            std::string bytes;
            tokenizing::EscapeError err = tokenizing::EscapeError::None;
            if (!tokenizing::decode_literal(lit->value, bytes, err)) return {};
            return make_string(m_types.intern_string(bytes));
        }

        case TK::True:  return make_bool(true);
        case TK::False: return make_bool(false);
        default: return {};
    }
}

ConstValue ConstEvaluator::eval_symbol(Symbol* s) {
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
                if (const std::string* sv = (*it)->lookup_svalue(s)) return make_string(sv);
            }
            return {};
        }

        case SymbolKind::EnumConstant: return eval_enum_constant(s);

        case SymbolKind::Variable: {
            if (!s->decl || s->decl->kind != K::VariableDeclaration) return {};
            auto* vd = static_cast<nodes::VariableDeclaration*>(s->decl);
            using RM = modifiers::RawModifiers;
            const auto& mods = vd->get_type_info().modifiers;
            if (!mods.has(RM::Constexpr) && !mods.has(RM::Const) && !mods.has(RM::Inline)) return {};
            if (!vd->has_initializer()) return {};
            const SubstEnv* env = env_of ? env_of(s) : nullptr;
            const bool cacheable = env != nullptr || m_types.subst_stack().empty();
            if (cacheable) if (auto it = m_symbol_cache.find(s); it != m_symbol_cache.end()) return it->second;
            if (!m_visiting.insert(s).second) return {};          
            if (env) m_types.push_subst(env);
            const std::size_t draws = m_random_draws;
            ConstValue v;
            if (mods.has(RM::Constexpr) || mods.has(RM::Constinit)) {
                RandomPermit permit(*this, vd);
                v = coerce(eval(vd->m_initializer), m_types.canonicalize(vd->get_type_info()));
            } else {
                v = coerce(eval(vd->m_initializer), m_types.canonicalize(vd->get_type_info()));
            }
            if (v.is_special()) v.fail = ConstValue::Fail::NotConstant;
            if (env) m_types.pop_subst();
            m_visiting.erase(s);
            if (cacheable && v.ok() && draws == m_random_draws) m_symbol_cache.emplace(s, v);
            return v;
        }

        default: return {};
    }
}

auto ConstEvaluator::compound_base(TK op) -> TK {
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

bool ConstEvaluator::is_constexpr_function(const nodes::FunctionDeclaration* fd) {
    return fd->get_modifiers().has(modifiers::RawModifiers::Constexpr)
        || fd->qualifiers().has(modifiers::FunctionQualifiers::Consteval);
}

auto ConstEvaluator::local_of(nodes::ASTNode* target) -> Local* {
    if (m_frames.empty() || !target) return nullptr;
    Symbol* s = nullptr;
    if      (target->kind == K::Identifier)          s = static_cast<nodes::Identifier*>(target)->resolved;
    else if (target->kind == K::QualifiedIdentifier) s = static_cast<nodes::QualifiedIdentifier*>(target)->resolved;
    if (!s) return nullptr;
    auto& locals = m_frames.back().locals;
    auto it = locals.find(s);
    return it == locals.end() ? nullptr : &it->second;
}

ConstValue ConstEvaluator::store(Local& slot, ConstValue v) {
    if (!v.ok()) return v;
    ConstValue fitted = slot.type ? coerce(v, slot.type) : v;
    if (fitted.ok()) slot.value = fitted;
    return fitted;
}

ConstValue ConstEvaluator::eval_assign(nodes::BinaryExpression* b) {
    Local* slot = local_of(b->left);
    if (!slot) return {};
    ConstValue r = eval(b->right);
    if (!r.ok()) return r;
    if (b->op == TK::Equal) return store(*slot, r);
    if (!slot->value.ok()) return {};
    return store(*slot, apply_binary(compound_base(b->op), slot->value, r, b));
}

ConstValue ConstEvaluator::eval_incdec(nodes::UnaryExpression* u) {
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

ConstValue ConstEvaluator::eval_call(nodes::CallExpression* c) {
    Symbol* fn = c->resolved;
    if (is_random_function(fn)) return eval_random(c, fn);
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

ConstValue ConstEvaluator::eval_value_function(Symbol* fn) {
    if (!fn || fn->kind != SymbolKind::Function || !fn->decl || fn->decl->kind != K::FunctionDeclaration) return {};
    auto* fd = static_cast<nodes::FunctionDeclaration*>(fn->decl);
    if (!is_constexpr_function(fd) || !fd->has_body()) return {};
    if (m_frames.size() >= kMaxCallDepth) return ConstValue::error(ConstValue::Fail::StepLimit);
    const SubstEnv* env = env_of ? env_of(fn) : nullptr;
    if (env) m_types.push_subst(env);
    Type* ret = m_types.canonicalize(fd->get_return_type());
    m_frames.push_back(Frame{});
    const Flow flow = exec(fd->get_body());
    ConstValue result = m_frames.back().result;
    m_frames.pop_back();
    if (env) m_types.pop_subst();
    if (flow == Flow::Fail)   return m_failure;
    if (flow != Flow::Return) return ConstValue::error(ConstValue::Fail::NoReturn);
    if (ret && ret->is_reference()) ret = static_cast<ReferenceType*>(ret)->referent();
    return coerce(result, ret);
}

bool ConstEvaluator::test(nodes::ASTNode* cond, bool& taken) {
    ConstValue v = eval(cond);
    bool ok = false;
    taken = truthy(v, ok);
    if (!ok) { m_failure = v.is_error() ? v : ConstValue{}; return false; }
    return true;
}

auto ConstEvaluator::exec_loop_body(nodes::ASTNode* body, bool& stop, std::string_view label) -> Flow {
    stop = false;
    const Flow f = exec(body);
    if (f == Flow::Return || f == Flow::Fail) { stop = true; return f; }

    if ((f == Flow::Break || f == Flow::Continue) && !m_jump_label.empty()) {
        if (m_jump_label != label) { stop = true; return f; }
        m_jump_label = {};
    }

    if (f == Flow::Break) stop = true;
    return Flow::Normal;
}

auto ConstEvaluator::exec(nodes::ASTNode* s) -> Flow {
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
                const Flow f = exec_loop_body(w->body, stop, w->label);
                if (stop) return f == Flow::Normal ? Flow::Normal : f;
                if (++m_steps > kMaxSteps) return fail(ConstValue::error(ConstValue::Fail::StepLimit));
            }
        }

        case K::DoWhileStatement: {
            auto* w = static_cast<nodes::DoWhileStatement*>(s);

            for (;;) {
                bool taken = false, stop = false;
                const Flow f = exec_loop_body(w->body, stop, w->label);
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
                const Flow f = exec_loop_body(fs->body, stop, fs->label);
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
                case V::Break:    m_jump_label = static_cast<nodes::SingleStatement*>(s)->label; return Flow::Break;
                case V::Continue: m_jump_label = static_cast<nodes::SingleStatement*>(s)->label; return Flow::Continue;
                default:          return fail({});
            }
        }

        case K::StaticAssertDeclaration: return Flow::Normal;
        default:                          return fail({});
    }
}

ConstValue ConstEvaluator::eval_enum_constant(Symbol* s) {
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

ConstValue ConstEvaluator::eval_binary(nodes::BinaryExpression* b) {
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

ConstValue ConstEvaluator::apply_binary(TK op, ConstValue l, ConstValue r, nodes::BinaryExpression* b) {
    if (!l.ok()) return l;
    if (!r.ok()) return r;

    if (l.is_string() || r.is_string()) {
        if (!l.is_string() || !r.is_string()) return {};
        switch (op) {
            case TK::Plus:         return make_string(m_types.intern_string(*l.s + *r.s));
            case TK::LogicEqual:   return make_bool(*l.s == *r.s);
            case TK::NotEqual:     return make_bool(*l.s != *r.s);
            case TK::LessThan:     return make_bool(*l.s < *r.s);
            case TK::GreaterThan:  return make_bool(*l.s > *r.s);
            case TK::LessEqual:    return make_bool(*l.s <= *r.s);
            case TK::GreaterEqual: return make_bool(*l.s >= *r.s);
            default:               return {};
        }
    }
    if ((l.is_special() || r.is_special()) && !l.is_bool() && !r.is_bool()) return apply_special(op, l, r, b);

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

ConstValue ConstEvaluator::eval_int_op(TK op, const WideInt& a, const WideInt& b, const IntShape& sh) {
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
            if (b.is_zero()) return a.is_zero() ? make_special(SpecialKind::Indeterminate, ConstValue::Fail::Indeterminate) : make_special(SpecialKind::Undefined, ConstValue::Fail::Undefined);
            return make_int(a / b);

        case TK::Percent:
            if (b.is_zero()) return a.is_zero() ? make_special(SpecialKind::Indeterminate, ConstValue::Fail::Indeterminate) : make_special(SpecialKind::Undefined, ConstValue::Fail::Undefined);
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

ConstValue ConstEvaluator::eval_int_pow(const WideInt& a, const WideInt& b) {
    if (a.is_undefined() || b.is_undefined()) return {};
    if (b.is_zero()) return a.is_zero() ? make_special(SpecialKind::Indeterminate, ConstValue::Fail::Indeterminate) : make_int(WideInt(1));
    const long long hb = a.highest_bit();        
    const bool odd = b.get_bit(0);

    if (b.is_negative()) {
        if (hb <  0) return make_special(SpecialKind::Undefined, ConstValue::Fail::Undefined);
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

ConstValue ConstEvaluator::guard_range(const WideFloat& r, const WideFloat& a, const WideFloat& b) {
    if (r.is_unordered()) return special_of_float(r, a.is_unordered() || b.is_unordered());
    const bool finite_in = a.is_finite() && b.is_finite();
    if (finite_in && r.is_infinite()) return ConstValue::error(ConstValue::Fail::Overflow);
    if (finite_in && r.is_zero() && !a.is_zero() && !b.is_zero()) return ConstValue::error(ConstValue::Fail::Underflow);
    return make_float(r);
}

ConstValue ConstEvaluator::eval_float_op(TK op, const WideFloat& a, const WideFloat& b) {
    switch (op) {
        case TK::Plus:     return guard_range(a + b, a, b);
        case TK::Minus:    return guard_range(a - b, a, b);
        case TK::Asterisk: return guard_range(a * b, a, b);
        case TK::Slash:    return guard_range(a / b, a, b);

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

ConstValue ConstEvaluator::eval_float_pow(const WideFloat& a, const WideFloat& b) {
    return guard_range(mp::math::pow(a, b), a, b);
}

ConstValue ConstEvaluator::finite_result(const WideFloat& r, const WideFloat& a, const WideFloat& b) {
    const bool inputs_finite = a.is_finite() && b.is_finite();
    if (r.is_unordered()) return special_of_float(r, a.is_unordered() || b.is_unordered());
    if (inputs_finite && r.is_infinite()) return ConstValue::error(ConstValue::Fail::Overflow);
    if (inputs_finite && r.is_zero() && !a.is_zero() && !b.is_zero()) return ConstValue::error(ConstValue::Fail::Underflow);
    return make_float(r);
}

ConstValue ConstEvaluator::convert_to(const ConstValue& v, BuiltinType* dst) {
    using BK = parser_types::PrimitiveType::BaseKind;

    if (v.is_special()) {
        if (dst->base() == BK::Bool) return make_bool(true);
        if (dst->base() != BK::Int && dst->base() != BK::Float) return {};
        if (v.sp == SpecialKind::NegInf && dst->is_unsigned()) return dst->base() == BK::Int ? make_int(WideInt()) : make_float(WideFloat(0));
        return v;
    }

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

ConstValue ConstEvaluator::bitnot_at_width(const WideInt& v, unsigned width, bool is_signed) {
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

} // namespace semantics
} // namespace walnut
