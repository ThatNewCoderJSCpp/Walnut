#include "Semantics/type_walker.hpp"
#include "Semantics/type_ops.hpp"
#include "Semantics/substitution.hpp"
#include "Common/arena_allocator.hpp"
#include "Common/error_reporter.hpp"

namespace walnut {
namespace semantics {

namespace {

Type* without_ref(Type* t) { return t && t->is_reference() ? static_cast<ReferenceType*>(t)->referent() : t; }

bool numeric_builtin(Type* t) {
    if (!t || !t->is_builtin()) return false;
    const auto base = static_cast<BuiltinType*>(t)->base();
    return base == parser_types::PrimitiveType::BaseKind::Int || base == parser_types::PrimitiveType::BaseKind::Float;
}

bool dynamic_builtin(Type* t) {
    return t && t->is_builtin() && static_cast<BuiltinType*>(t)->base() == parser_types::PrimitiveType::BaseKind::Dynamic;
}

bool void_builtin(Type* t) {
    return t && t->is_builtin() && static_cast<BuiltinType*>(t)->base() == parser_types::PrimitiveType::BaseKind::Void;
}

bool lvalue_ref(Type* t) {
    return t && t->is_reference() && static_cast<ReferenceType*>(t)->ref_qual() == RefQual::LValue;
}

} // namespace

std::optional<std::int64_t> TypeWalker::lifecycle_trait(parser_types::TypeTrait trait, const std::vector<Type*>& args) {
    using T = parser_types::TypeTrait;
    for (Type* a : args) if (!a || a->is_error() || a->is_dependent()) return std::nullopt;
    if (args.empty()) return std::nullopt;
    const std::vector<Type*> rest(args.begin() + 1, args.end());

    switch (trait) {
        case T::IsConstructible:          return can_construct(args[0], rest, Want::Exists, 0) ? 1 : 0;
        case T::IsNothrowConstructible:   return can_construct(args[0], rest, Want::NoThrow, 0) ? 1 : 0;
        case T::IsConstexprConstructible: return can_construct(args[0], rest, Want::Constexpr, 0) ? 1 : 0;
        case T::IsAssignable:             return args.size() == 2 && can_assign(args[0], args[1], Want::Exists, 0) ? 1 : 0;
        case T::IsNothrowAssignable:      return args.size() == 2 && can_assign(args[0], args[1], Want::NoThrow, 0) ? 1 : 0;
        case T::IsConstexprAssignable:    return args.size() == 2 && can_assign(args[0], args[1], Want::Constexpr, 0) ? 1 : 0;
        case T::IsDestructible:
        case T::IsNothrowDestructible:    return can_destroy(args[0], 0) ? 1 : 0;
        case T::IsCallable:
            if (!probe_function_like(args[0]) && !dynamic_builtin(without_ref(args[0]))) return 0;
            return probe_compiles(args, [this](const std::vector<nodes::ASTNode*>& ids) -> nodes::ASTNode* {
                auto* call = make_in<nodes::CallExpression>(m_inst->arena(), ids[0], 0);
                for (std::size_t i = 1; i < ids.size(); ++i) call->m_arguments.push_back(ids[i]);
                return call;
            }, false) ? 1 : 0;
        case T::IsFunctionLike:           return probe_function_like(args[0]) ? 1 : 0;
        case T::CanForEach:               return probe_for_each(args[0]) ? 1 : 0;
        case T::HasOperator:              return probe_operator(args, false) ? 1 : 0;
        case T::OverloadsOperator:        return probe_operator(args, true) ? 1 : 0;
        default:                          return std::nullopt;
    }
}

bool TypeWalker::probe_compiles(const std::vector<Type*>& types, const std::function<nodes::ASTNode*(const std::vector<nodes::ASTNode*>&)>& build, bool want_overload) {
    if (!m_inst || types.empty()) return false;
    ArenaScope arena(m_inst->arena());
    DiagnosticTrap trap(m_reporter);
    std::vector<nodes::ASTNode*> ids;
    for (std::size_t i = 0; i < types.size(); ++i) {
        if (void_builtin(without_ref(types[i]))) {
            trap.discard();
            return false;
        }
        Symbol* sym = synthetic_symbol("walnut_probe_" + std::to_string(i), types[i], nullptr);
        ids.push_back(ident_for(sym, 0));
    }
    nodes::ASTNode* e = build(ids);
    bool ok = false;
    if (e) {
        walk_expr(e);
        Type* t = type_of(e);
        ok = t && !t->is_error() && !contains_error(t);
    }
    const bool errors = trap.discard();
    if (!ok || errors) return false;
    Symbol* chosen = nullptr;
    switch (e->kind) {
        case K::BinaryExpression:      chosen = static_cast<nodes::BinaryExpression*>(e)->resolved; break;
        case K::UnaryExpression:       chosen = static_cast<nodes::UnaryExpression*>(e)->resolved; break;
        case K::DereferenceExpression: chosen = static_cast<nodes::DereferenceExpression*>(e)->resolved; break;
        case K::SubscriptExpression:   chosen = static_cast<nodes::SubscriptExpression*>(e)->resolved; break;
        case K::CallExpression:        chosen = static_cast<nodes::CallExpression*>(e)->resolved; break;
        default: break;
    }
    const bool written = chosen && chosen->decl && chosen->decl->kind == K::OperatorFunctionDeclaration;
    if (written) {
        bool any_dynamic = false;
        for (Type* t : types) if (dynamic_builtin(m_types.strip_cv(without_ref(t)))) any_dynamic = true;
        if (!any_dynamic) {
            for (const ParamShape& p : shapes_of(chosen)) {
                if (p.element && dynamic_builtin(m_types.strip_cv(without_ref(p.element)))) return false;
            }
        }
    }
    if (!want_overload) return true;
    return written;
}

bool TypeWalker::probe_function_like(Type* t) {
    Type* c = without_ref(t);
    if (!c || c->is_error()) return false;
    c = m_types.strip_cv(c);
    if (c->is_function() || c->kind() == TypeKind::Closure) return true;
    if (c->is_pointer()) {
        Type* p = static_cast<PointerType*>(c)->pointee();
        return p && m_types.strip_cv(p)->is_function();
    }
    if (!record_symbol_of(c)) return false;
    Scope* ms = member_scope_of(c);
    if (!ms) return false;
    for (Symbol* o = ms->find_member("operator"); o; o = o->next_overload) {
        if (o->decl && o->decl->kind == K::OperatorFunctionDeclaration && static_cast<nodes::OperatorFunctionDeclaration*>(o->decl)->get_overload() == nodes::OverloadableOperator::Call) return true;
    }
    return ms->find_member("operator(())") != nullptr;
}

bool TypeWalker::probe_for_each(Type* t) {
    Type* c = without_ref(t);
    if (!c || c->is_error()) return false;
    if (iterated_element(c)) return true;
    if (!m_inst || !record_symbol_of(m_types.strip_cv(c))) return false;
    Scope* ms = member_scope_of(c);
    if (!ms || !ms->find_member("begin") || !ms->find_member("end")) return false;
    ArenaScope arena(m_inst->arena());
    DiagnosticTrap trap(m_reporter);
    Symbol* range = synthetic_symbol("walnut_probe_range", m_types.reference(c, RefQual::LValue), nullptr);
    auto member_call = [&](std::string_view name) -> nodes::ASTNode* {
        auto* m = make_in<nodes::MemberAccessExpression>(m_inst->arena(), ident_for(range, 0), name, nodes::MemberAccessExpression::Op::Dot, 0);
        auto* call = make_in<nodes::CallExpression>(m_inst->arena(), m, 0);
        walk_expr(call);
        return call;
    };
    auto usable = [](Type* x) { return x && !x->is_error() && !contains_error(x); };
    Type* it = without_ref(type_of(member_call("begin")));
    Type* et = without_ref(type_of(member_call("end")));
    bool ok = usable(it) && usable(et);
    if (ok) {
        Symbol* it_sym = synthetic_symbol("walnut_probe_it", m_types.strip_cv(it), nullptr);
        Symbol* end_sym = synthetic_symbol("walnut_probe_end", m_types.strip_cv(et), nullptr);
        nodes::ASTNode* cond = binary_node(ident_for(it_sym, 0), tokenizing::Token::Kind::NotEqual, ident_for(end_sym, 0), 0);
        auto* next = make_in<nodes::UnaryExpression>(m_inst->arena(), ident_for(it_sym, 0), tokenizing::Token::Kind::DoublePlus, true, 0);
        auto* get = make_in<nodes::DereferenceExpression>(m_inst->arena(), ident_for(it_sym, 0), 0);
        walk_expr(cond);
        walk_expr(next);
        walk_expr(get);
        ok = usable(type_of(cond)) && usable(type_of(next)) && usable(type_of(get));
    }
    const bool errors = trap.discard();
    return ok && !errors;
}

bool TypeWalker::probe_operator(const std::vector<Type*>& args, bool want_overload) {
    using TK = tokenizing::Token::Kind;
    if (args.size() < 2) return false;
    Symbol* tag = record_symbol_of(m_types.strip_cv(without_ref(args[0])));
    if (!tag) return false;
    enum class Shape { Binary, Prefix, Deref, Subscript, Call };
    struct OpInfo { std::string_view name; Shape shape; TK token; };
    static const OpInfo ops[] = {
        { "add", Shape::Binary, TK::Plus }, { "subtract", Shape::Binary, TK::Minus }, { "multiply", Shape::Binary, TK::Asterisk },
        { "divide", Shape::Binary, TK::Slash }, { "modulo", Shape::Binary, TK::Percent }, { "power", Shape::Binary, TK::DoubleAsterisk },
        { "equal", Shape::Binary, TK::LogicEqual }, { "not_equal", Shape::Binary, TK::NotEqual }, { "less", Shape::Binary, TK::LessThan },
        { "greater", Shape::Binary, TK::GreaterThan }, { "less_equal", Shape::Binary, TK::LessEqual }, { "greater_equal", Shape::Binary, TK::GreaterEqual },
        { "logical_and", Shape::Binary, TK::LogicAnd }, { "logical_or", Shape::Binary, TK::LogicOr },
        { "bit_and", Shape::Binary, TK::Ampersand }, { "bit_or", Shape::Binary, TK::Pipe }, { "bit_xor", Shape::Binary, TK::Caret },
        { "shift_left", Shape::Binary, TK::DoubleLessThan }, { "shift_right", Shape::Binary, TK::DoubleGreaterThan },
        { "assign", Shape::Binary, TK::Equal }, { "add_assign", Shape::Binary, TK::PlusEqual }, { "subtract_assign", Shape::Binary, TK::MinusEqual },
        { "multiply_assign", Shape::Binary, TK::AsteriskEqual }, { "divide_assign", Shape::Binary, TK::SlashEqual }, { "modulo_assign", Shape::Binary, TK::PercentEqual },
        { "power_assign", Shape::Binary, TK::DoubleAsteriskEqual }, { "bit_and_assign", Shape::Binary, TK::AmpersandEqual }, { "bit_or_assign", Shape::Binary, TK::PipeEqual },
        { "bit_xor_assign", Shape::Binary, TK::CaretEqual }, { "shift_left_assign", Shape::Binary, TK::ShiftLeftEqual }, { "shift_right_assign", Shape::Binary, TK::ShiftRightEqual },
        { "negate", Shape::Prefix, TK::Minus }, { "positive", Shape::Prefix, TK::Plus }, { "logical_not", Shape::Prefix, TK::ExclamationMark },
        { "bit_not", Shape::Prefix, TK::Tilde }, { "increment", Shape::Prefix, TK::DoublePlus }, { "decrement", Shape::Prefix, TK::DoubleMinus },
        { "dereference", Shape::Deref, TK::Asterisk }, { "subscript", Shape::Subscript, TK::LeftSquare }, { "call", Shape::Call, TK::LeftParen },
    };
    const OpInfo* op = nullptr;
    for (const OpInfo& o : ops) if (o.name == tag->name) op = &o;
    if (!op) return false;
    std::vector<Type*> operands(args.begin() + 1, args.end());
    switch (op->shape) {
        case Shape::Binary:
        case Shape::Subscript:
            if (operands.size() == 1) operands.push_back(operands[0]);
            if (operands.size() != 2) return false;
            break;
        case Shape::Prefix:
        case Shape::Deref:
            if (operands.size() != 1) return false;
            break;
        case Shape::Call:
            break;
    }
    return probe_compiles(operands, [this, op](const std::vector<nodes::ASTNode*>& ids) -> nodes::ASTNode* {
        switch (op->shape) {
            case Shape::Binary:    return binary_node(ids[0], op->token, ids[1], 0);
            case Shape::Prefix:    return make_in<nodes::UnaryExpression>(m_inst->arena(), ids[0], op->token, true, 0);
            case Shape::Deref:     return make_in<nodes::DereferenceExpression>(m_inst->arena(), ids[0], 0);
            case Shape::Subscript: return make_in<nodes::SubscriptExpression>(m_inst->arena(), ids[0], ids[1], 0);
            case Shape::Call: {
                auto* call = make_in<nodes::CallExpression>(m_inst->arena(), ids[0], 0);
                for (std::size_t i = 1; i < ids.size(); ++i) call->m_arguments.push_back(ids[i]);
                return call;
            }
        }
        return nullptr;
    }, want_overload);
}

bool TypeWalker::symbol_nothrow(Symbol* s) {
    if (!s) return false;
    if (Instantiator::Instantiation* I = m_inst ? m_inst->owning(s) : nullptr) ensure_typed(I);
    if (!m_throws) return false;
    return m_throws->analyzer.declared_or_inferred(s) == ThrowSpec::NoThrow;
}

bool TypeWalker::symbol_constexpr(const Symbol* s) {
    using RM = modifiers::RawModifiers;
    using FQ = modifiers::FunctionQualifiers;
    if (!s || !s->decl) return false;
    switch (s->decl->kind) {
        case K::ConstructorDeclaration: {
            const auto* c = static_cast<const nodes::ConstructorDeclaration*>(s->decl);
            return c->m_modifiers.has(RM::Constexpr) || c->m_qualifiers.has(FQ::Consteval);
        }
        case K::OperatorFunctionDeclaration: {
            const auto* o = static_cast<const nodes::OperatorFunctionDeclaration*>(s->decl);
            return o->m_modifiers.has(RM::Constexpr) || o->m_qualifiers.has(FQ::Consteval);
        }
        case K::FunctionDeclaration: {
            const auto* f = static_cast<const nodes::FunctionDeclaration*>(s->decl);
            return f->get_modifiers().has(RM::Constexpr) || f->qualifiers().has(FQ::Consteval);
        }
        default:
            return false;
    }
}

TypeWalker::ConstructChoice TypeWalker::quiet_constructor(Type* record, const std::vector<Type*>& args) {
    ConstructChoice out;
    Symbol* rec = nullptr;
    if (!record_decl_of(record, rec) || !rec->inner_scope) return out;
    EnvGuard g(m_types, env_of_symbol(rec));
    Type* self = record_type_of(rec);
    Symbol* bucket = rec->inner_scope->find_member("constructor");

    if (bucket) {
        std::vector<CandidateMatch> matches;
        std::vector<Symbol*> cands;
        std::vector<Instantiator::Instantiation*> cand_inst;

        for (Symbol* o = bucket; o; o = o->next_overload) {
            if (o->template_decl) {
                if (!m_inst) continue;
                auto* I = m_inst->instantiate_for_call(o, {}, args, nullptr, nullptr);
                if (!I || !I->sym) continue;
                matches.push_back(match_arguments(I->fn_shapes, args, m_types));
                cands.push_back(I->sym);
                cand_inst.push_back(I);
                continue;
            }
            if (!o->decl || o->decl->kind != K::ConstructorDeclaration) continue;
            auto* cd = static_cast<nodes::ConstructorDeclaration*>(o->decl);
            if (cd->is_deleted() && self_ref_kind(cd->get_parameters(), self) == 2) continue;
            matches.push_back(match_arguments(shapes_of(o), args, m_types));
            cands.push_back(o);
            cand_inst.push_back(nullptr);
        }

        if (!cands.empty()) {
            std::vector<std::size_t> fixed;
            std::vector<bool> from_template;
            for (std::size_t k = 0; k < cands.size(); ++k) {
                fixed.push_back(fixed_parameter_count(cands[k]));
                from_template.push_back(cand_inst[k] != nullptr);
            }
            const Selection sel = select_overload(matches, from_template, fixed);
            if (sel.status == SelectStatus::Ambiguous) return out;
            if (sel.status == SelectStatus::Ok) {
                Symbol* pick = cands[sel.index];
                if (cand_inst[sel.index]) ensure_typed(cand_inst[sel.index]);
                const auto* cd = pick->decl && pick->decl->kind == K::ConstructorDeclaration ? static_cast<const nodes::ConstructorDeclaration*>(pick->decl) : nullptr;
                if (cd && cd->is_deleted()) return out;
                out.ok = true;
                if (cd && cd->is_defaulted()) out.implicit = true;
                else out.ctor = pick;
                return out;
            }
        }
    }

    if (args.empty()) {
        if (!default_construct_failure(self).empty()) return out;
        out.ok = true;
        out.implicit = true;
        return out;
    }

    if (args.size() == 1 && is_copy_source(rec, args[0])) {
        out.ok = true;
        out.implicit = true;
    }

    return out;
}

bool TypeWalker::members_can(Type* record, int kind, Want want, int depth) {
    using RM = modifiers::RawModifiers;
    Symbol* owner = nullptr;
    nodes::RecordDeclaration* rd = record_decl_of(record, owner);
    if (!rd) return false;
    EnvGuard g(m_types, env_of_symbol(owner));

    const auto source = [this, kind](Type* t) -> Type* {
        if (kind == 0) return nullptr;
        if (kind == 1) {
            CV c = t->cv();
            c.is_const = true;
            return m_types.reference(m_types.with_cv(t, c), RefQual::LValue);
        }
        return m_types.reference(t, RefQual::RValue);
    };

    const auto part = [&](Type* t) -> bool {
        if (!t) return false;
        if (kind == 0) return can_construct(t, {}, want, depth + 1);
        if (t->is_reference()) return true;
        return can_construct(t, { source(m_types.strip_cv(t)) }, want, depth + 1);
    };

    for (parser_types::TypeInfo* info : rd->all_bases()) {
        if (!part(m_types.canonicalize(*info))) return false;
    }

    for (const nodes::RecordDeclaration::Member& m : rd->get_members()) {
        if (!m.node) continue;

        if (m.node->kind == K::VariableDeclaration) {
            auto* v = static_cast<nodes::VariableDeclaration*>(m.node);
            const auto& mods = v->get_type_info().modifiers;
            if (mods.has(RM::Static)) continue;
            if (want == Want::Constexpr && (mods.has(RM::Lazy) || mods.has(RM::LazyThreadsafe))) return false;
            Type* ft = m_types.canonicalize(v->get_type_info());

            if (kind == 0 && v->has_initializer()) {
                if (want == Want::NoThrow && !(m_throws && m_throws->analyzer.of_expr(v->m_initializer).nothrow())) return false;
                if (want == Want::Constexpr && !m_eval.eval(v->m_initializer).ok()) return false;
                continue;
            }

            if (!part(ft)) return false;
        } else if (m.node->kind == K::ArrayDeclaration) {
            auto* a = static_cast<nodes::ArrayDeclaration*>(m.node);
            if (a->get_array_modifiers().has(RM::Static)) continue;
            if (kind == 0 && a->has_initializer()) {
                if (want == Want::Constexpr) return false;
                continue;
            }
            if (!part(array_symbol_type(a))) return false;
        }
    }

    return true;
}

bool TypeWalker::can_construct(Type* t, const std::vector<Type*>& args, Want want, int depth) {
    if (!t || depth > 24) return false;

    if (t->is_reference()) {
        if (args.size() != 1) return false;
        Type* referent = static_cast<ReferenceType*>(t)->referent();
        if (lvalue_ref(t) && !referent->cv().is_const && !lvalue_ref(args[0])) return false;
        return rank_conversion(args[0], t, m_types) != ConversionRank::None;
    }

    Type* v = m_types.strip_cv(t);
    if (void_builtin(v) || v->is_function()) return false;

    if (v->is_record()) {
        std::string error;
        if (eval_type_trait(m_types, parser_types::TypeTrait::IsAbstract, { v }, error).value_or(0) != 0) return false;
        const ConstructChoice c = quiet_constructor(v, args);
        if (!c.ok) return false;
        if (want == Want::Exists) return true;
        if (c.ctor) return want == Want::NoThrow ? symbol_nothrow(c.ctor) : symbol_constexpr(c.ctor);
        const int kind = args.empty() ? 0 : (lvalue_ref(args[0]) ? 1 : 2);
        return members_can(v, kind, want, depth + 1);
    }

    if (v->is_array()) {
        auto* arr = static_cast<ArrayType*>(v);
        if (!arr->extent()) return want != Want::Constexpr && args.empty();
        if (args.empty()) return can_construct(arr->element(), {}, want, depth + 1);
        if (args.size() != 1 || m_types.strip_cv(without_ref(args[0])) != v) return false;
        Type* element = arr->element();
        return can_construct(element, { lvalue_ref(args[0]) ? m_types.reference(element, RefQual::LValue) : m_types.reference(element, RefQual::RValue) }, want, depth + 1);
    }

    if (want == Want::Constexpr && (dynamic_builtin(v) || v->kind() == TypeKind::Variant || v->kind() == TypeKind::Closure)) return false;
    if (args.empty()) return true;
    if (args.size() != 1) return false;
    Type* from = m_types.strip_cv(without_ref(args[0]));
    if (numeric_builtin(from) && numeric_builtin(v)) return true;
    return rank_conversion(args[0], v, m_types) != ConversionRank::None;
}

bool TypeWalker::can_assign(Type* to, Type* from, Want want, int depth) {
    using RM = modifiers::RawModifiers;
    if (!to || !from || depth > 24) return false;
    Type* target = without_ref(to);
    if (target->cv().is_const) return false;
    Type* v = m_types.strip_cv(target);
    if (void_builtin(v) || v->is_function()) return false;

    if (v->is_record()) {
        Symbol* chosen = nullptr;
        resolve_member_operator(v, nodes::OverloadableOperator::Assign, { from }, &chosen);

        if (chosen) {
            const auto* od = chosen->decl && chosen->decl->kind == K::OperatorFunctionDeclaration ? static_cast<const nodes::OperatorFunctionDeclaration*>(chosen->decl) : nullptr;
            if (od && od->is_deleted()) return false;
            if (want == Want::Exists) return true;
            return want == Want::NoThrow ? symbol_nothrow(chosen) : symbol_constexpr(chosen);
        }

        Symbol* owner = nullptr;
        nodes::RecordDeclaration* rd = record_decl_of(v, owner);
        if (!rd || !is_copy_source(owner, from)) return false;
        EnvGuard g(m_types, env_of_symbol(owner));
        const bool copy = lvalue_ref(from);

        const auto part = [&](Type* t) -> bool {
            if (!t || t->is_reference()) return false;
            Type* plain = m_types.strip_cv(t);
            Type* src = nullptr;
            if (copy) {
                CV c = plain->cv();
                c.is_const = true;
                src = m_types.reference(m_types.with_cv(plain, c), RefQual::LValue);
            } else {
                src = m_types.reference(plain, RefQual::RValue);
            }
            return can_assign(t, src, want, depth + 1);
        };

        for (parser_types::TypeInfo* info : rd->all_bases()) {
            if (!part(m_types.canonicalize(*info))) return false;
        }

        for (const nodes::RecordDeclaration::Member& m : rd->get_members()) {
            if (!m.node) continue;
            if (m.node->kind == K::VariableDeclaration) {
                auto* f = static_cast<nodes::VariableDeclaration*>(m.node);
                if (f->get_type_info().modifiers.has(RM::Static)) continue;
                if (!part(m_types.canonicalize(f->get_type_info()))) return false;
            } else if (m.node->kind == K::ArrayDeclaration) {
                auto* a = static_cast<nodes::ArrayDeclaration*>(m.node);
                if (a->get_array_modifiers().has(RM::Static)) continue;
                if (!part(array_symbol_type(a))) return false;
            }
        }

        return true;
    }

    if (v->is_array()) {
        auto* arr = static_cast<ArrayType*>(v);
        if (!arr->extent() || m_types.strip_cv(without_ref(from)) != v) return false;
        Type* element = arr->element();
        return can_assign(element, lvalue_ref(from) ? m_types.reference(element, RefQual::LValue) : m_types.reference(element, RefQual::RValue), want, depth + 1);
    }

    if (want == Want::Constexpr && (dynamic_builtin(v) || v->kind() == TypeKind::Variant || v->kind() == TypeKind::Closure)) return false;
    return rank_conversion(from, v, m_types) != ConversionRank::None;
}

bool TypeWalker::can_destroy(Type* t, int depth) {
    if (!t || depth > 24) return false;
    if (t->is_reference()) return true;
    Type* v = m_types.strip_cv(t);
    if (void_builtin(v) || v->is_function()) return false;
    if (v->is_array()) return can_destroy(static_cast<ArrayType*>(v)->element(), depth + 1);
    if (!v->is_record()) return true;
    Symbol* owner = nullptr;
    nodes::RecordDeclaration* rd = record_decl_of(v, owner);
    if (!rd) return true;

    for (const nodes::RecordDeclaration::Member& m : rd->get_members()) {
        if (m.node && m.node->kind == K::DestructorDeclaration && static_cast<nodes::DestructorDeclaration*>(m.node)->is_deleted()) return false;
    }

    EnvGuard g(m_types, env_of_symbol(owner));
    for (parser_types::TypeInfo* info : rd->all_bases()) {
        if (!can_destroy(m_types.canonicalize(*info), depth + 1)) return false;
    }

    return true;
}

} // namespace semantics
} // namespace walnut
