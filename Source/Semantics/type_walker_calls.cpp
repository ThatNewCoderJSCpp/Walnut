#include "Semantics/type_walker.hpp"
#include "Semantics/type_ops.hpp"
#include "Parser/Nodes/node_children.hpp"
#include <unordered_set>
#include <algorithm>
#include <deque>
#include "Lexer/numeric_literal.hpp"

namespace walnut {
namespace semantics {

bool TypeWalker::is_generic_lambda(const nodes::LambdaExpression* l) {
    if (l->generic_origin || !l->get_parameters()) return false;
    for (const nodes::FunctionParameter* p : l->get_parameters()->m_params) if (prim_is(p->get_type(), parser_types::PrimitiveType::BaseKind::Auto)) return true;
    return false;
}

nodes::LambdaExpression* TypeWalker::specialize_lambda(nodes::LambdaExpression* gl, const std::vector<Type*>& args, nodes::ASTNode* site, bool quiet) {
    if (!m_inst || !gl->get_parameters()) return nullptr;
    const auto& params = gl->get_parameters()->m_params;
    std::vector<Type*> key;

    for (Type* a : args) {
        if (a && a->is_reference()) a = static_cast<ReferenceType*>(a)->referent();
        key.push_back(m_types.strip_cv(a));
    }

    if (key.size() != params.size()) {
        if (!quiet && site) SemanticError::no_matching_overload(m_reporter, file_of(site), site->line, "lambda");
        return nullptr;
    }

    for (std::size_t i = 0; i < gl->spec_keys.size(); ++i) {
        if (gl->spec_keys[i] != key) continue;
        if (gl->spec_failed[i]) return nullptr;
        return gl->specializations[i];
    }

    for (Type* k : key) if (!k || k->is_error()) return nullptr;
    auto* clone = static_cast<nodes::LambdaExpression*>(gl->clone_into(m_inst->arena()));
    clone->generic_origin = gl;
    Scope* parent = gl->scope ? gl->scope->parent : m_current;
    if (m_inst->build_scopes) m_inst->build_scopes(clone, parent);
    if (m_inst->resolve_names) m_inst->resolve_names(clone, parent);

    for (std::size_t i = 0; i < params.size(); ++i) {
        parser_types::TypeInfo& ti = clone->get_parameters()->m_params[i]->get_type();
        if (!prim_is(ti, parser_types::PrimitiveType::BaseKind::Auto)) continue;
        Type* t = key[i];
        if (ti.modifiers.has(modifiers::RawModifiers::Const)) t = add_const(t);

        for (const parser_types::IndirectionQualifier& q : ti.indirection) {
            if (q.is_pointer()) t = m_types.pointer(t);
            else t = m_types.reference(t, q.is_rvalue_ref() ? RefQual::RValue : RefQual::LValue);
        }

        ti.canonical = t;
    }

    const std::size_t index = gl->specializations.size();
    gl->specializations.push_back(clone);
    gl->spec_keys.push_back(key);
    gl->spec_failed.push_back(false);
    Scope* saved = m_current;
    m_current = parent;
    DiagnosticTrap trap(m_reporter);
    walk_expr(clone);
    const bool failed = quiet ? trap.discard() : trap.commit();
    m_current = saved;

    if (failed && quiet) {
        gl->specializations.erase(gl->specializations.begin() + static_cast<std::ptrdiff_t>(index));
        gl->spec_keys.erase(gl->spec_keys.begin() + static_cast<std::ptrdiff_t>(index));
        gl->spec_failed.erase(gl->spec_failed.begin() + static_cast<std::ptrdiff_t>(index));
        return nullptr;
    }

    if (failed) {
        gl->spec_failed[index] = true;
        return nullptr;
    }

    return clone;
}

bool TypeWalker::is_pack_spread(const nodes::ASTNode* a) {
    return a && a->kind == K::UnaryExpression && static_cast<const nodes::UnaryExpression*>(a)->op == tokenizing::Token::Kind::Ellipsis;
}

void TypeWalker::expand_pack_arguments(nodes::CallExpression* c) {
    expand_pack_list(c->m_arguments, true);
}

Type* TypeWalker::runtime_spread_element(nodes::ASTNode* a) {
    if (!a || !m_runtime_spreads.count(a)) return nullptr;
    Type* t = type_of(static_cast<nodes::UnaryExpression*>(a)->operand);
    if (t && t->is_reference()) t = static_cast<ReferenceType*>(t)->referent();
    t = t ? m_types.strip_cv(t) : nullptr;
    return t && t->is_array() ? static_cast<ArrayType*>(t)->element() : nullptr;
}

void TypeWalker::expand_pack_list(ArenaVector<nodes::ASTNode*>& items, bool allow_runtime) {
    bool any = false;
    for (nodes::ASTNode* a : items) any = any || is_pack_spread(a);
    if (!any) return;
    std::vector<nodes::ASTNode*> out;

    for (nodes::ASTNode* a : items) {
        if (!is_pack_spread(a)) { out.push_back(a); continue; }
        nodes::ASTNode* pattern = static_cast<nodes::UnaryExpression*>(a)->operand;
        Symbol* pack = find_pack_ref(pattern);
        nodes::FunctionParameter* p = variadic_param_of(pack);
        std::vector<Symbol*> pks;
        if (p) collect_pack_params(m_types.canonicalize(p->get_type()), pks);

        if (p && p->expanded_pack && m_inst) {
            for (std::size_t k = pack_offset_of(pattern, pack); k < p->pack_symbols.size(); ++k) {
                nodes::ASTNode* clone = clone_resolved(pattern);
                substitute_symbol(clone, pack, p->pack_symbols[k]);
                substitute_pack_types(clone, pks, k);
                out.push_back(clone);
            }
            continue;
        }

        if (!p || pks.empty()) {
            Type* spread = nullptr;
            if (allow_runtime) {
                walk_expr(pattern);
                spread = type_of(pattern);
                if (spread && spread->is_reference()) spread = static_cast<ReferenceType*>(spread)->referent();
                spread = spread ? m_types.strip_cv(spread) : nullptr;
            }
            if (spread && spread->is_array()) m_runtime_spreads.insert(a);
            else SemanticError::emit(m_reporter, file_of(a), a->line, "Type", "'...' spreads a template parameter pack, a variadic parameter or an array into a call's arguments; this isn't one of those");
        }
        out.push_back(a);
    }

    items = std::move(out);
}

void TypeWalker::type_fold(nodes::FoldExpression* f) {
    using Form = nodes::FoldExpression::Form;
    using TK = tokenizing::Token::Kind;
    f->lowered = f->sequence = f->first = f->step = f->init = nullptr;
    f->element = f->accumulator = nullptr;
    nodes::ASTNode* pattern = nullptr;
    nodes::ASTNode* init = nullptr;

    if (f->form == Form::UnaryLeft) pattern = f->rhs;
    else if (f->form == Form::UnaryRight) pattern = f->lhs;
    else if (find_pack_ref(f->lhs)) { pattern = f->lhs; init = f->rhs; }
    else { pattern = f->rhs; init = f->lhs; }

    const bool left = f->form == Form::UnaryLeft || (f->form == Form::Binary && pattern == f->rhs);
    Symbol* pack = find_pack_ref(pattern);
    nodes::FunctionParameter* p = variadic_param_of(pack);

    if (!p || !m_inst) {
        walk_expr(f->lhs); walk_expr(f->rhs);
        if (!p) SemanticError::emit(m_reporter, file_of(f), f->line, "Type", "a fold expression needs a variadic parameter to expand");
        assign_typed(f, nullptr, VC::RValue);
        return;
    }

    if (p->expanded_pack) {
        std::vector<nodes::ASTNode*> elems;
        for (std::size_t k = pack_offset_of(pattern, pack); k < p->pack_symbols.size(); ++k) {
            nodes::ASTNode* c = clone_resolved(pattern);
            substitute_symbol(c, pack, p->pack_symbols[k]);
            elems.push_back(c);
        }

        nodes::ASTNode* acc = nullptr;

        if (elems.empty()) {
            if (init) acc = init;
            else if (f->op == TK::LogicAnd) acc = make_in<nodes::Literal>(m_inst->arena(), std::string_view("true"), TK::True, f->line);
            else if (f->op == TK::LogicOr) acc = make_in<nodes::Literal>(m_inst->arena(), std::string_view("false"), TK::False, f->line);
            else if (f->op != TK::Comma) SemanticError::emit(m_reporter, file_of(f), f->line, "Type", "a fold over an empty pack needs an initial value");
        } else if (left) {
            acc = init ? binary_node(init, f->op, elems.front(), f->line) : elems.front();
            for (std::size_t i = 1; i < elems.size(); ++i) acc = binary_node(acc, f->op, elems[i], f->line);
        } else {
            acc = init ? binary_node(elems.back(), f->op, init, f->line) : elems.back();
            for (std::size_t i = elems.size() - 1; i-- > 0;) acc = binary_node(elems[i], f->op, acc, f->line);
        }

        if (acc) acc->file_id = f->file_id;
        f->lowered = acc;
        walk_expr(acc);
        assign_typed(f, acc ? type_of(acc) : m_types.void_(), VC::RValue);
        return;
    }

    Type* seq = symbol_type(pack);
    if (!seq || seq->is_dependent() || !m_types.strip_cv(seq)->is_array()) {
        walk_expr(f->lhs); walk_expr(f->rhs);
        assign_typed(f, nullptr, VC::RValue);
        return;
    }

    Type* el = static_cast<ArrayType*>(m_types.strip_cv(seq))->element();
    const std::string tag = std::to_string(m_inst->kept_names());
    Symbol* es = synthetic_symbol("fold#e" + tag, el, f);
    f->sequence = ident_for(pack, f->line);
    walk_expr(f->sequence);
    f->element = es;
    f->right_to_left = !left;
    nodes::ASTNode* first = clone_resolved(pattern);
    substitute_symbol(first, pack, es);
    walk_expr(first);
    f->first = first;

    if (f->op == TK::Comma) {
        walk_expr(init);
        f->init = init;
        assign_typed(f, m_types.void_(), VC::RValue);
        return;
    }

    Type* pt = type_of(first);
    if (pt && pt->is_reference()) pt = static_cast<ReferenceType*>(pt)->referent();
    pt = m_types.strip_cv(pt);
    Symbol* acc = synthetic_symbol("fold#a" + tag, pt, f);
    f->accumulator = acc;
    nodes::ASTNode* again = clone_resolved(pattern);
    substitute_symbol(again, pack, es);
    nodes::ASTNode* step = left ? binary_node(ident_for(acc, f->line), f->op, again, f->line) : binary_node(again, f->op, ident_for(acc, f->line), f->line);
    step->file_id = f->file_id;
    walk_expr(step);
    Type* rt = type_of(step);
    if (rt && rt->is_reference()) rt = static_cast<ReferenceType*>(rt)->referent();
    rt = m_types.strip_cv(rt);

    if (rt && rt != pt) {
        acc->bound_type = rt;
        walk_expr(step);
    }

    f->step = step;

    if (init) {
        walk_expr(init);
        f->init = init;
        if (Type* it = type_of(init); it && rt) check_binding(init, it, rt, f);
    }

    assign_typed(f, rt, VC::RValue);
}

Type* TypeWalker::type_of_literal(nodes::Literal* lit) {
    using BK = parser_types::PrimitiveType::BaseKind;
    using TK = tokenizing::Token::Kind;
    switch (lit->get_kind()) {
        case TK::Integer: {
            const tokenizing::NumericLiteral n = tokenizing::parse_numeric_literal(lit->value);

            if (n.base == 10 && !n.is_float && n.digits.size() > 1 && n.digits[0] == '0') {
                SemanticWarning::emit(m_warnings, file_of(lit), lit->line, "Literal", "'" + std::string(lit->value) + "' is read as the decimal number " + n.digits.substr(n.digits.find_first_not_of('0') == std::string::npos ? n.digits.size() - 1 : n.digits.find_first_not_of('0')) + "; a leading 0 doesn't make an octal number in Walnut, so write '0o" + n.digits.substr(1) + "' if you meant octal");
            }

            if (n.has_suffix && n.error.empty() && !n.digits.empty()) {
                Type* t = m_types.builtin_ranked(n.type_is_float ? BK::Float : BK::Int, n.rank, n.is_unsigned);
                const WideInt* raw = m_types.consts().parse(n.digits, n.base);
                if (raw && n.type_is_float) {
                    WideFloat r;
                    if (wf_round_to_width(wf_from_int(*raw), bit_width_of_rank(n.rank), r) && r.is_infinite()) SemanticError::emit(m_reporter, file_of(lit), lit->line, "Literal", "'" + std::string(lit->value) + "' is too large for '" + type_str(t) + "', which can't represent it");
                } else if (raw && !ConstTable::fits(*raw, bit_width_of_rank(n.rank), !n.is_unsigned)) {
                    report_overflow(lit, "'" + std::string(lit->value) + "' doesn't fit in '" + type_str(t) + "', so it becomes inf");
                }
                return t;
            }

            ConstValue v = m_eval.eval(lit);
            if (v.is_int()) for (int rank = 0; rank <= 6; ++rank) if (ConstTable::fits(*v.i, bit_width_of_rank(rank), true)) return m_types.builtin_ranked(BK::Int, rank);
            if (v.is_int()) SemanticError::emit(m_reporter, file_of(lit), lit->line, "Literal", "'" + std::string(lit->value) + "' is too large for any integer type; the widest is 'long long integer' (2048 bits)");
            return m_types.builtin(BK::Int);
        }
        case TK::Float: {
            const tokenizing::NumericLiteral n = tokenizing::parse_numeric_literal(lit->value);
            const WideFloat* raw = n.error.empty() ? m_types.floats().parse(n.body) : nullptr;
            if (raw && raw->is_infinite()) SemanticError::emit(m_reporter, file_of(lit), lit->line, "Literal", "'" + std::string(lit->value) + "' is too large to represent in any float type");
            if (!n.has_suffix || !n.error.empty()) return m_types.builtin(BK::Float);
            Type* t = m_types.builtin_ranked(BK::Float, n.rank, n.is_unsigned);
            WideFloat r;

            if (raw && !raw->is_infinite() && wf_round_to_width(*raw, bit_width_of_rank(n.rank), r)) {
                if (r.is_infinite()) SemanticError::emit(m_reporter, file_of(lit), lit->line, "Literal", "'" + std::string(lit->value) + "' is too large for '" + type_str(t) + "', which can't represent it");
                else if (r.is_zero() && !raw->is_zero()) SemanticWarning::emit(m_warnings, file_of(lit), lit->line, "Underflow", "'" + std::string(lit->value) + "' is too small for '" + type_str(t) + "', so it becomes 0");
            }

            return t;
        }
        case TK::InfinityKeyword:
        case TK::NanKeyword:
        case TK::UndefinedKeyword:
        case TK::IndeterminateKeyword: return m_types.special_value();
        case TK::String:          return m_types.builtin(BK::String);
        case TK::TextLiteral:     return m_types.builtin(BK::Text);
        case TK::Character:       return m_types.builtin(BK::Char);
        case TK::True:
        case TK::False:           return m_types.bool_();
        case TK::NullptrKeyword:  return m_types.null_();
        case TK::DefaultKeyword: {
            auto it = m_default_args.find(lit);
            return it != m_default_args.end() ? it->second : m_types.error_();
        }
        case TK::ThisKeyword:
            if (m_self_by_value && lit != m_receiver_this) {
                safety_error(lit, lit->line, "this method takes 'self' by value, so it works on its own copy and 'this' isn't that copy; use 'self'");
            }
            if (in_friend_body()) {
                SemanticError::emit(m_reporter, file_of(lit), lit->line, "Name", "a friend function isn't a member of the class, so it has no 'this'; take the object as a parameter instead");
                return m_types.error_();
            }
            return this_type();
        default:                  return m_types.dynamic_();
    }
}

void TypeWalker::rebase_on_instance(nodes::RecordDeclaration* r) {
    if (!m_inst || !r->scope) return;

    for (parser_types::TypeInfo* info : r->all_bases()) {
        Type* bt = m_types.strip_cv(m_types.canonicalize(*info));
        if (!bt || !bt->is_record() || bt->is_dependent()) continue;
        auto* rt = static_cast<RecordType*>(bt);
        if (!rt->is_instantiation()) continue;
        Instantiator::Instantiation* I = m_inst->for_record(rt, r);
        if (!I || !I->sym || !I->sym->inner_scope) continue;
        Scope* inst_scope = I->sym->inner_scope;
        Symbol* generic = info->resolved;
        bool replaced = false;

        for (Scope*& b : r->scope->bases) {
            if (b == inst_scope) { replaced = true; break; }
            if (generic && b == generic->inner_scope) { b = inst_scope; replaced = true; break; }
        }

        if (!replaced) r->scope->bases.push_back(inst_scope);
        ensure_typed(I);
    }
}

void TypeWalker::check_member_ambiguity(Scope* members, std::string_view name, nodes::ASTNode* site) {
    if (!members || !site || members->bases.size() < 2 || members->find_local(name)) return;
    std::vector<const Scope*> owners;
    members->member_owners(name, owners);
    if (owners.size() < 2) return;
    auto label = [](const Scope* s) { return s && s->owner_symbol ? "'" + std::string(s->owner_symbol->name) + "'" : std::string("a base class"); };
    std::string from = label(owners[0]);
    for (std::size_t i = 1; i < owners.size(); ++i) from += (i + 1 == owners.size() ? " and " : ", ") + label(owners[i]);
    const std::string where = members->owner_symbol ? " in '" + std::string(members->owner_symbol->name) + "'" : std::string();
    bool same = true;
    for (const Scope* o : owners) same = same && o == owners[0];
    const std::string fix = "; declare '" + std::string(name) + "' in this class to decide which one it means";
    if (same) SemanticError::emit(m_reporter, file_of(site), site->line, "Inheritance", "'" + std::string(name) + "' is ambiguous" + where + ": " + label(owners[0]) + " is a base class more than once, through different parents, so there are " + std::to_string(owners.size()) + " copies of '" + std::string(name) + "'" + fix);
    else SemanticError::emit(m_reporter, file_of(site), site->line, "Inheritance", "'" + std::string(name) + "' is ambiguous" + where + ": it is inherited from " + from + fix);
}

void TypeWalker::refresh_member(nodes::Identifier* id) {
    Symbol* s = id->resolved;
    if (!s || !s->owner || s->owner->kind != Scope::Kind::Record) return;

    for (Scope* sc = m_current; sc; sc = sc->parent) {
        if (sc->kind != Scope::Kind::Record) continue;
        Symbol* again = sc->find_member(s->name);
        if (again && again != s && again->kind == s->kind) id->resolved = again;
        return;
    }
}

Type* TypeWalker::record_type_of(Symbol* sym) {
    if (m_types.instance_type_hook) if (Type* t = m_types.instance_type_hook(sym)) return t;
    return m_types.record(sym, {}, CV{});
}

Type* TypeWalker::this_type() {
    Type* rec = enclosing_record_type();
    if (rec && rec->is_record() && m_in_const_method) rec = add_const(rec);
    return (rec && rec->is_record()) ? m_types.pointer(rec) : rec;
}

Type* TypeWalker::enclosing_record_type() {
    for (Scope* s = m_current; s; s = s->parent) {       
        if (s->node && s->node->kind == K::RecordDeclaration)
            if (Symbol* sym = static_cast<nodes::RecordDeclaration*>(s->node)->symbol) return record_type_of(sym);
    }

    return m_types.dynamic_();
}

Type* TypeWalker::type_binary(nodes::BinaryExpression* b, Symbol*& resolved_op) {
    resolved_op = nullptr;
    Type* l = type_of(b->left); 
    Type* r = type_of(b->right);
    const tokenizing::Token::Kind op = b->op;
    if (op == tokenizing::Token::Kind::Comma) return r;

    if (is_assignment_op(op)) {
        const std::string why = lazy_write_problem(b->left);
        if (!why.empty()) {
            SemanticError::emit(m_reporter, file_of(b), b->line, "Lazy", why);
            return l;
        }
    }

    if (record_symbol_of(l) || record_symbol_of(r)) {
        const nodes::OverloadableOperator want = nodes::classify_single_operator(op);
        m_operator_site = b;
        const ArenaVector<nodes::ASTNode*> right_operand{ b->right };
        if (record_symbol_of(l) && !record_symbol_of(r)) m_operator_args = &right_operand;
        OpResult res = resolve_operator(want, { l, r });
        m_operator_args = nullptr;
        m_operator_site = nullptr;
        b->negate_result = false;
        b->swap_operands = false;
        b->memberwise    = nullptr;
        if (res.status == SelectStatus::Ok) { resolved_op = res.chosen; return res.type; }

        if (res.status == SelectStatus::NoMatch && (is_comparison_op(op) || is_equality_op(op))) {
            if (Type* t = synthesize_comparison(b, l, r, resolved_op)) return t;
        }

        if (is_logical_op(op) && res.status == SelectStatus::NoMatch) {
            if (l && !is_bool_testable(l)) SemanticError::condition_not_bool(m_reporter, file_of(b->left),  b->left->line,  type_str(l));
            if (r && !is_bool_testable(r)) SemanticError::condition_not_bool(m_reporter, file_of(b->right), b->right->line, type_str(r));
            return m_types.bool_();
        }

        if (op == tokenizing::Token::Kind::Equal && res.status == SelectStatus::NoMatch && is_dynamic_t(m_types.strip_cv(l && l->is_reference() ? static_cast<ReferenceType*>(l)->referent() : l))) {
            report_not_dynamic(r, b);
            return l;
        }

        if (op == tokenizing::Token::Kind::Equal && res.status == SelectStatus::NoMatch && record_symbol_of(l)) {
            if (!is_lvalue(b->left)) SemanticError::not_assignable(m_reporter, file_of(b), b->line, "assign to", false);
            else if (type_is_const(l)) SemanticError::not_assignable(m_reporter, file_of(b), b->line, "assign to", true);
            if (r) check_binding(b->right, r, m_types.strip_cv(l->is_reference() ? static_cast<ReferenceType*>(l)->referent() : l), b);
            return l;
        }

        if (res.status == SelectStatus::Ambiguous) {
            SemanticError::ambiguous_call(m_reporter, file_of(b), b->line, nodes::overloadable_operator_name(want));
        } else if (const std::string why = is_equality_op(op) ? equality_failure_of(l, r) : std::string(); !why.empty()) {
            SemanticError::emit(m_reporter, file_of(b), b->line, "Overload", "'" + type_str(m_types.strip_cv(l->is_reference() ? static_cast<ReferenceType*>(l)->referent() : l)) + "' has no '" + nodes::overloadable_operator_name(want) + "' operator, and one cannot be generated: " + why);
        } else {
            SemanticError::no_matching_overload(m_reporter, file_of(b), b->line, nodes::overloadable_operator_name(want));
        }

        return nullptr;
    }

    if (!check_builtin_operands(b, l, r) && !is_assignment_op(op)) {
        return (is_comparison_op(op) || is_equality_op(op)) ? m_types.bool_() : l;
    }

    if (is_assignment_op(op)) {
        if (!known_t(l)) {
        } else if (!is_lvalue(b->left)) {
            SemanticError::not_assignable(m_reporter, file_of(b), b->line, "assign to", false);
        } else if (type_is_const(l)) {
            SemanticError::not_assignable(m_reporter, file_of(b), b->line, "assign to", true);
        }

        if (l && r) {
            using TKind = tokenizing::Token::Kind;
            Type* lv = m_types.strip_cv(l->is_reference() ? static_cast<ReferenceType*>(l)->referent() : l);
            Type* rv = m_types.strip_cv(r->is_reference() ? static_cast<ReferenceType*>(r)->referent() : r);
            const bool r_integer = rv && rv->is_builtin() && static_cast<BuiltinType*>(rv)->base() == parser_types::PrimitiveType::BaseKind::Int;
            if (b->op != TKind::Equal && is_string_view_t(lv)) SemanticError::emit(m_reporter, file_of(b), b->line, "Type", "a string_view is read-only, so it can't be changed with a compound assignment; build the result in a string instead");
            if (b->op == TKind::Equal) { check_binding(b->right, r, l, b); check_view_source(b->right, r, l, b, false); }
            else if (lv && lv->is_pointer() && (b->op == TKind::PlusEqual || b->op == TKind::MinusEqual) && (r_integer || is_dynamic_t(rv))) {}
            else if ((b->op == TKind::ShiftLeftEqual || b->op == TKind::ShiftRightEqual) && r_integer) {}
            else if ((b->op == TKind::PlusEqual || b->op == TKind::MinusEqual) && r_integer && lv && lv->is_builtin() && static_cast<BuiltinType*>(lv)->base() == parser_types::PrimitiveType::BaseKind::Char) {}
            else if (b->op == TKind::PlusEqual && lv && lv->is_builtin() && (static_cast<BuiltinType*>(lv)->base() == parser_types::PrimitiveType::BaseKind::String || static_cast<BuiltinType*>(lv)->base() == parser_types::PrimitiveType::BaseKind::Text)
                     && rv && rv->is_builtin() && static_cast<BuiltinType*>(rv)->base() == parser_types::PrimitiveType::BaseKind::Char) {}
            else if (!constant_fits(b->right, r, l)) check_convertible(r, l, b);
        }
        return l;                                        
    }

    if (is_logical_op(op)) {                             
        if (l && !is_bool_testable(l)) SemanticError::condition_not_bool(m_reporter, file_of(b->left),  b->left->line,  type_str(l));
        if (r && !is_bool_testable(r)) SemanticError::condition_not_bool(m_reporter, file_of(b->right), b->right->line, type_str(r));
        return m_types.bool_();
    }

    if (is_comparison_op(op) || is_equality_op(op)) return m_types.bool_();
    if (is_dynamic_t(l) || is_dynamic_t(r)) return m_types.dynamic_();   

    if (op == tokenizing::Token::Kind::Plus && l && r) {
        if (operand_class(l) == OperandClass::String) return owning_string(m_types.strip_cv(l->is_reference() ? static_cast<ReferenceType*>(l)->referent() : l));
        if (operand_class(r) == OperandClass::String) return owning_string(m_types.strip_cv(r->is_reference() ? static_cast<ReferenceType*>(r)->referent() : r));
    }

    if ((op == tokenizing::Token::Kind::Plus || op == tokenizing::Token::Kind::Minus) && l && r) {
        const OperandClass lc = operand_class(l), rc = operand_class(r);
        if (lc == OperandClass::Char && rc == OperandClass::Int) return m_types.builtin(parser_types::PrimitiveType::BaseKind::Char);
        if (op == tokenizing::Token::Kind::Plus && lc == OperandClass::Int && rc == OperandClass::Char) return m_types.builtin(parser_types::PrimitiveType::BaseKind::Char);
    }

    if (l && r) {
        if (Type* adapted = adapt_literal(b->right, r, l)) return adapted;
        if (Type* adapted = adapt_literal(b->left, l, r)) return adapted;
    }

    if (op == tokenizing::Token::Kind::Minus && l && r) {
        Type* lp = m_types.strip_cv(l->is_reference() ? static_cast<ReferenceType*>(l)->referent() : l);
        Type* rp = m_types.strip_cv(r->is_reference() ? static_cast<ReferenceType*>(r)->referent() : r);
        if (lp && rp && lp->is_pointer() && rp->is_pointer()) return m_types.builtin(parser_types::PrimitiveType::BaseKind::Int, parser_types::LengthModifier::Long);
    }

    if (l && r) {
        auto plain = [this](Type* t) { return m_types.strip_cv(t->is_reference() ? static_cast<ReferenceType*>(t)->referent() : t); };
        if (plain(l)->is_special_value() && plain(r)->is_special_value()) return m_types.builtin(parser_types::PrimitiveType::BaseKind::Float);
        if (rank_conversion(r, l, m_types) != ConversionRank::None) return l;
        if (rank_conversion(l, r, m_types) != ConversionRank::None) return r;
        if (Type* common = numeric_common_type(m_types, l, r)) return common;
    }

    return l ? l : r;
}

Type* TypeWalker::type_unary(nodes::UnaryExpression* u, Symbol*& resolved_op) {
    using K = tokenizing::Token::Kind;
    resolved_op = nullptr;
    Type* t = type_of(u->operand);

    if (u->op == K::ExclamationMark) {
        if (record_symbol_of(t)) {
            OpResult res = resolve_operator(nodes::OverloadableOperator::LogicalNot, { t });
            if (res.status == SelectStatus::Ok) { resolved_op = res.chosen; return res.type; }
        }

        if (t && !is_bool_testable(t)) SemanticError::not_convertible(m_reporter, file_of(u), u->line, type_str(t), "bool");
        return m_types.bool_();
    }

    if (u->op == K::DoublePlus || u->op == K::DoubleMinus) {
        if (record_symbol_of(t)) {
            nodes::OverloadableOperator want = nodes::classify_single_operator(u->op);
            OpResult res = resolve_incdec(t, want, !u->is_prefix());
            if (res.status == SelectStatus::Ok) { resolved_op = res.chosen; return res.type; }
            if (res.status == SelectStatus::Ambiguous) SemanticError::ambiguous_call(m_reporter, file_of(u), u->line, nodes::overloadable_operator_name(want));
            else                                       SemanticError::no_matching_overload(m_reporter, file_of(u), u->line, nodes::overloadable_operator_name(want));
            return nullptr;
        }

        const char* what = (u->op == K::DoublePlus) ? "apply '++' to" : "apply '--' to";
        const char* sym  = (u->op == K::DoublePlus) ? "++" : "--";

        if (!known_t(t))             {}
        else if (!is_lvalue(u->operand))  SemanticError::not_assignable(m_reporter, file_of(u), u->line, what, false);
        else if (type_is_const(t))   SemanticError::not_assignable(m_reporter, file_of(u), u->line, what, true);

        if (t && !is_arithmetic_or_pointer(t) && !is_dynamic_t(t)) SemanticError::bad_operand(m_reporter, file_of(u), u->line, sym, type_str(t));
        return t;
    }

    if (record_symbol_of(t)) {                      
        const nodes::OverloadableOperator want = nodes::classify_single_operator(u->op);
        m_operator_site = u;
        OpResult res = resolve_operator(want, { t });
        m_operator_site = nullptr;
        if (res.status == SelectStatus::Ok) { resolved_op = res.chosen; return res.type; }
        if (res.status == SelectStatus::Ambiguous) SemanticError::ambiguous_call(m_reporter, file_of(u), u->line, nodes::overloadable_operator_name(want));
        else                                       SemanticError::no_matching_overload(m_reporter, file_of(u), u->line, nodes::overloadable_operator_name(want));
        return nullptr;
    }

    if (u->op == K::Minus && t && !is_numeric_t(t) && !is_dynamic_t(t) && !m_types.strip_cv(t->is_reference() ? static_cast<ReferenceType*>(t)->referent() : t)->is_special_value()) SemanticError::bad_operand(m_reporter, file_of(u), u->line, "-", type_str(t));

    if (u->op == K::Minus && u->operand && u->operand->kind == nodes::ASTNode::Kind::Literal && static_cast<nodes::Literal*>(u->operand)->get_kind() == K::Integer) {
        const tokenizing::NumericLiteral n = tokenizing::parse_numeric_literal(static_cast<nodes::Literal*>(u->operand)->value);
        if (n.is_unsigned && n.digits.find_first_not_of('0') != std::string::npos) SemanticWarning::emit(m_warnings, file_of(u), u->line, "Underflow", "'-" + std::string(static_cast<nodes::Literal*>(u->operand)->value) + "' negates an unsigned number, so it wraps around to a large positive value");
    }

    return t;
}

Type* TypeWalker::common_type(Type* a, Type* b, nodes::ASTNode* site) {
    if (!a) return b;
    if (!b) return a;
    if (a == b) return a;
    if (is_dynamic_t(a) || is_dynamic_t(b)) return m_types.dynamic_();
    if (rank_conversion(b, a, m_types) != ConversionRank::None) return a;
    if (rank_conversion(a, b, m_types) != ConversionRank::None) return b;
    if (Type* n = numeric_common_type(m_types, a, b)) return n;
    SemanticError::no_common_type(m_reporter, file_of(site), site->line, type_str(a), type_str(b));
    return a;
}

void TypeWalker::walk_param_defaults(const nodes::FunctionParameters* ps) {
    if (!ps) return;

    for (nodes::FunctionParameter* p : ps->m_params) {
        if (!p || !p->has_initializer()) continue;
        nodes::ASTNode* d = const_cast<nodes::ASTNode*>(p->get_initializer());
        walk_expr(d);
        Type* want = m_types.canonicalize(p->get_type());
        Type* got = type_of(d);
        if (want && got && !is_dynamic_t(want)) check_binding(d, got, want, d);
    }
}

void TypeWalker::refine_by_value_category(std::vector<CandidateMatch>& matches, const std::vector<std::vector<ParamShape>>& shapes_list, Span<nodes::ASTNode* const> args, std::size_t offset) {
    for (std::size_t k = 0; k < matches.size() && k < shapes_list.size(); ++k) {
        if (!matches[k].viable) continue;
        const std::vector<ParamShape>& shapes = shapes_list[k];
        for (std::size_t j = 0; j < shapes.size() && j < args.size(); ++j) {
            if (shapes[j].is_pack || !is_lvalue(args[j])) continue;
            Type* at = type_of(args[j]);
            Type* pt = shapes[j].element;
            if (!at || !pt || rank_conversion(at, pt, m_types) != ConversionRank::UserDefined) continue;
            Symbol* via = nullptr;
            Type* want = pt->is_reference() ? static_cast<ReferenceType*>(pt)->referent() : pt;
            if (rank_user_defined(at, m_types.strip_cv(want), m_types, false, &via) != ConversionRank::None && rvalue_only_conversion(via)) { matches[k].viable = false; break; }
        }
    }

    if (matches.size() > 1) {
        std::vector<CandidateMatch> by_category = matches;
        bool any = false;

        for (std::size_t k = 0; k < by_category.size(); ++k) {
            if (!by_category[k].viable) continue;
            const std::vector<ParamShape>& shapes = shapes_list[k];

            for (std::size_t j = 0; j < shapes.size() && j < args.size(); ++j) {
                if (shapes[j].is_pack) break;
                Type* pt = shapes[j].element;
                if (!pt || !pt->is_reference()) continue;
                auto* rt = static_cast<ReferenceType*>(pt);
                const bool lvalue_arg = is_lvalue(args[j]);
                if (rt->ref_qual() == RefQual::LValue && !rt->referent()->cv().is_const && !lvalue_arg) by_category[k].viable = false;
                if (rt->ref_qual() == RefQual::RValue && lvalue_arg) by_category[k].viable = false;
            }


            any = any || by_category[k].viable;
        }

        if (any) {
            for (std::size_t j = 0; j < args.size(); ++j) {
                if (is_lvalue(args[j])) continue;
                bool rvalue_ref_here = false;
                std::vector<Type*> rvalue_referents;

                for (std::size_t k = 0; k < by_category.size(); ++k) {
                    if (!by_category[k].viable || j >= shapes_list[k].size() || shapes_list[k][j].is_pack) continue;
                    Type* pt = shapes_list[k][j].element;
                    if (pt && pt->is_reference() && static_cast<ReferenceType*>(pt)->ref_qual() == RefQual::RValue) {
                        rvalue_ref_here = true;
                        rvalue_referents.push_back(m_types.strip_cv(static_cast<ReferenceType*>(pt)->referent()));
                    }
                }

                if (!rvalue_ref_here) continue;

                for (std::size_t k = 0; k < by_category.size(); ++k) {
                    if (!by_category[k].viable || j >= shapes_list[k].size() || shapes_list[k][j].is_pack || j + offset >= by_category[k].ranks.size()) continue;
                    Type* pt = shapes_list[k][j].element;
                    if (!pt || !pt->is_reference() || static_cast<ReferenceType*>(pt)->ref_qual() != RefQual::LValue) continue;
                    ConversionRank& r = by_category[k].ranks[j + offset];
                    Type* referent = m_types.strip_cv(static_cast<ReferenceType*>(pt)->referent());
                    const bool same_target = std::find(rvalue_referents.begin(), rvalue_referents.end(), referent) != rvalue_referents.end();
                    if (r == ConversionRank::Exact) r = ConversionRank::Promotion;
                    else if (same_target && r == ConversionRank::Promotion) r = ConversionRank::Conversion;
                    else if (same_target && r == ConversionRank::Conversion) r = ConversionRank::UserDefined;
                    else if (same_target && r == ConversionRank::UserDefined) by_category[k].viable = false;
                }
            }

            matches = std::move(by_category);
        }
    }
}

bool TypeWalker::is_template_instance(Symbol* s) {
    if (!s || !m_inst || s->kind != SymbolKind::Function) return false;
    Instantiator::Instantiation* I = m_inst->owning(s);
    return I && I->sym == s && I->source && I->source->m_declaration && I->source->m_declaration->kind == K::FunctionDeclaration;
}

Symbol* TypeWalker::generic_of_instance(Symbol* s) {
    if (!s || !m_inst || s->kind != SymbolKind::Function) return s;
    Instantiator::Instantiation* I = m_inst->owning(s);
    if (!I || I->sym != s || !I->source || !I->source->m_declaration || I->source->m_declaration->kind != K::FunctionDeclaration) return s;
    Symbol* primary = static_cast<nodes::FunctionDeclaration*>(I->source->m_declaration)->symbol;
    if (!primary) return s;
    if (primary->owner) if (Symbol* head = primary->owner->find_local(primary->name)) return head;
    return primary;
}

Symbol* TypeWalker::overload_origin(Symbol* s) {
    if (!s || !m_inst) return s;
    Instantiator::Instantiation* I = m_inst->owning(s);
    if (!I || I->sym != s || !I->source || !I->source->m_declaration) return s;
    nodes::ASTNode* d = I->source->m_declaration;
    Symbol* primary = nullptr;
    if (d->kind == K::FunctionDeclaration) primary = static_cast<nodes::FunctionDeclaration*>(d)->symbol;
    else if (d->kind == K::OperatorFunctionDeclaration) primary = static_cast<nodes::OperatorFunctionDeclaration*>(d)->symbol;
    else if (d->kind == K::ConstructorDeclaration) primary = static_cast<nodes::ConstructorDeclaration*>(d)->symbol;
    return primary ? primary : s;
}

void TypeWalker::remember_overloads(nodes::ASTNode* site, const std::vector<CandidateMatch>& matches, const std::vector<Symbol*>& origins, std::size_t chosen) {
    std::vector<Symbol*> order;

    for (std::size_t i : candidate_order(matches, chosen)) {
        if (i >= origins.size() || !origins[i]) continue;
        if (std::find(order.begin(), order.end(), origins[i]) == order.end()) order.push_back(origins[i]);
    }

    if (site) m_types.overload_orders[site] = std::move(order);
    else m_types.last_overload_order = std::move(order);
}

void TypeWalker::keep_overloads(nodes::ASTNode* site, Symbol* chosen) {
    std::vector<Symbol*> order;
    order.swap(m_types.last_overload_order);
    if (!site || !chosen || order.empty() || order.front() != overload_origin(chosen)) return;
    m_types.overload_orders[site] = std::move(order);
}

Type* TypeWalker::type_call(nodes::CallExpression* c, const std::vector<Type*>& args, VC& vc) {
    vc = VC::RValue;
    if (c->m_callee && c->m_callee->kind == K::Identifier) refresh_member(static_cast<nodes::Identifier*>(c->m_callee));
    Symbol* callee_sym = generic_of_instance(callee_symbol(c->m_callee));

    if (Type* constructed = constructed_type(c->m_callee, callee_sym, c)) {
        check_not_abstract(constructed, c);
        c->resolved = resolve_construction(constructed, args, c);
        return constructed;
    }

    struct BraceHook {
        TypeContext& t; std::function<ConversionRank(std::size_t, Type*)> saved;
        BraceHook(TypeContext& c, std::function<ConversionRank(std::size_t, Type*)> f) : t(c), saved(c.brace_arg_rank) { c.brace_arg_rank = std::move(f); }
        ~BraceHook() { t.brace_arg_rank = saved; }
    };
    BraceHook brace_hook(m_types, [this, c](std::size_t a, Type* p) { return a < c->m_arguments.size() ? rank_brace_arg(c->m_arguments[a], p) : ConversionRank::None; });
    struct SpreadHook {
        TypeContext& t; std::function<Type*(std::size_t, std::size_t)> saved;
        SpreadHook(TypeContext& c, std::function<Type*(std::size_t, std::size_t)> f) : t(c), saved(c.spread_arg_element) { c.spread_arg_element = std::move(f); }
        ~SpreadHook() { t.spread_arg_element = saved; }
    };
    bool any_spread = false;
    for (nodes::ASTNode* a : c->m_arguments) any_spread = any_spread || m_runtime_spreads.count(a);
    std::function<Type*(std::size_t, std::size_t)> spread_lookup;
    if (any_spread) spread_lookup = [this, c](std::size_t a, std::size_t n) { return n == c->m_arguments.size() && a < n ? runtime_spread_element(c->m_arguments[a]) : nullptr; };
    SpreadHook spread_hook(m_types, std::move(spread_lookup));

    if (callee_sym && callee_sym->kind == SymbolKind::Function) {
        const std::size_t constraint_failures_before = m_inst ? m_inst->constraint_failures : 0;
        std::vector<CandidateMatch>                 matches;
        std::vector<Symbol*>                        cand_syms;
        std::vector<Instantiator::Instantiation*>   cand_inst;
        std::vector<std::vector<ParamShape>>        cand_shapes;
        std::vector<Symbol*>                        cand_origins;

        for (Symbol* o = callee_sym; o; o = o->next_overload) {
            if (o->template_decl) {                         
                if (!m_inst) continue;
                std::vector<bool> lvalues;
                lvalues.reserve(c->m_arguments.size());
                for (nodes::ASTNode* a : c->m_arguments) lvalues.push_back(a && is_lvalue(a));
                auto* I = m_inst->instantiate_for_call(o, c->get_template_args(), args, c, &lvalues);
                if (!I) continue;                           
                matches.push_back(match_with_constants(c, I->fn_shapes, args));
                cand_origins.push_back(o);
                cand_syms.push_back(I->sym);
                cand_inst.push_back(I);
                cand_shapes.push_back(I->fn_shapes);
                continue;
            }

            Instantiator::Instantiation* owner = m_inst ? m_inst->owning(o) : nullptr;

            {
                EnvGuard g(m_types, owner ? &owner->env : nullptr);
                matches.push_back(match_with_constants(c, shapes_of(o), args));
                cand_shapes.push_back(shapes_of(o));
            }

            cand_origins.push_back(o);
            cand_syms.push_back(o);
            cand_inst.push_back(owner);
        }

        {
            const int obj_const = call_object_constness(c);

            for (std::size_t k = 0; k < matches.size(); ++k) {
                ConversionRank object_rank = ConversionRank::Exact;
                Symbol* cs = cand_syms[k];
                if (obj_const >= 0 && is_member_function(cs) && !is_static_member_function(cs) && is_const_method(cs) != (obj_const == 1)) {
                    object_rank = ConversionRank::Promotion;
                }
                matches[k].ranks.insert(matches[k].ranks.begin(), object_rank);
            }
        }

        refine_by_value_category(matches, cand_shapes, c->m_arguments, 1);

        {
            std::size_t viable = 0;
            for (const CandidateMatch& m : matches) if (m.viable) ++viable;

            if (viable > 1) {
                for (std::size_t k = 0; k < matches.size(); ++k) {
                    if (!matches[k].viable || !cand_inst[k] || cand_inst[k]->sym != cand_syms[k]) continue;
                    for (std::size_t j : cand_inst[k]->forwarded_args) {
                        if (j + 1 < matches[k].ranks.size() && matches[k].ranks[j + 1] == ConversionRank::Exact) matches[k].ranks[j + 1] = ConversionRank::Promotion;
                    }
                }
            }
        }

        std::vector<bool> from_template;
        for (Symbol* s : cand_syms) from_template.push_back(is_template_instance(s));
        std::vector<std::size_t> fixed_params;
        for (Symbol* s : cand_syms) fixed_params.push_back(fixed_parameter_count(s));
        Selection sel = select_overload(matches, from_template, fixed_params);

        if (sel.status == SelectStatus::NoMatch) {
            if (m_inst && m_inst->constraint_failures != constraint_failures_before) {
                SemanticError::constraints_not_satisfied(m_reporter, file_of(c), c->line, callee_name(c->m_callee), m_last_constraint_failure);
            } else {
                SemanticError::no_matching_overload(m_reporter, file_of(c), c->line, callee_name(c->m_callee), candidate_notes(callee_sym, args, &c->m_arguments, callee_name(c->m_callee)));
            }
            return nullptr;
        }
        if (sel.status == SelectStatus::Ambiguous) { SemanticError::ambiguous_call(m_reporter, file_of(c), c->line, callee_name(c->m_callee)); return nullptr; }
        Symbol* chosen = cand_syms[sel.index];
        ensure_member_body(chosen);
        c->resolved = chosen;
        remember_overloads(c, matches, cand_origins, sel.index);
        check_const_call(chosen, c);
        auto note_rvalue = [c](Type* ret) {
            c->returns_rvalue_ref = ret && ret->is_reference() && static_cast<ReferenceType*>(ret)->ref_qual() == RefQual::RValue;
            return ret;
        };

        if (Instantiator::Instantiation* I = cand_inst[sel.index]) {
            ensure_typed(I);                                 
            { EnvGuard g(m_types, &I->env); check_arg_bindings(c, shapes_of(chosen)); }
            if (I->type && I->type->is_function()) return decay_ref(async_result(chosen, note_rvalue(static_cast<FunctionType*>(I->type)->ret())), vc);
            EnvGuard g(m_types, &I->env);
            Type* ft = chosen->type ? m_types.canonicalize(*chosen->type) : nullptr;
            if (ft && ft->is_function()) note_rvalue(static_cast<FunctionType*>(ft)->ret());
            return decay_ref(async_result(chosen, ft), vc);
        }

        check_arg_bindings(c, shapes_of(chosen));
        Type* ft = chosen->type ? m_types.canonicalize(*chosen->type) : nullptr;
        if (ft && ft->is_function()) note_rvalue(static_cast<FunctionType*>(ft)->ret());
        return decay_ref(async_result(chosen, ft), vc);
    }

    Type* callee_type = type_of(c->m_callee);
    c->closure_spec = nullptr;

    if (Type* ct = m_types.strip_cv(callee_type && callee_type->is_reference() ? static_cast<ReferenceType*>(callee_type)->referent() : callee_type); ct && ct->kind() == TypeKind::Closure) {
        auto* gl = static_cast<nodes::LambdaExpression*>(static_cast<ClosureType*>(ct)->lambda());
        nodes::LambdaExpression* spec = specialize_lambda(gl, args, c, false);
        c->closure_spec = spec;
        FunctionType* sf = spec ? as_function_type(type_of(spec)) : nullptr;
        if (!sf) return nullptr;
        for (std::size_t i = 0; i < args.size() && i < sf->params().size(); ++i) if (args[i] && sf->params()[i]) check_binding(c->m_arguments[i], args[i], sf->params()[i], c, false);
        return decay_ref(sf->ret(), vc);
    }

    if (FunctionType* ft = as_function_type(callee_type)) {
        const std::vector<Type*>& params = ft->params();
        if (ft->has_param_info()) {
            if (!match_arguments(shapes_of_function_type(ft), args, m_types).viable) SemanticError::no_matching_overload(m_reporter, file_of(c), c->line, callee_name(c->m_callee));
            return decay_ref(ft->ret(), vc);
        }

        if (args.size() != params.size()) {
            SemanticError::no_matching_overload(m_reporter, file_of(c), c->line, callee_name(c->m_callee));
        } else {
            for (std::size_t i = 0; i < args.size(); ++i) if (args[i] && params[i]) check_binding(c->m_arguments[i], args[i], params[i], c, false);
        }

        return decay_ref(ft->ret(), vc);
    }

    if (record_symbol_of(callee_type)) {
        std::vector<Type*> operands;
        operands.reserve(args.size() + 1);
        operands.push_back(callee_type);
        for (Type* a : args) operands.push_back(a);
        OpResult res = resolve_operator(nodes::OverloadableOperator::Call, operands);
        if (res.status == SelectStatus::Ok)        { c->resolved = res.chosen; keep_overloads(c, res.chosen); return decay_ref(res.type, vc); }
        if (res.status == SelectStatus::Ambiguous) { SemanticError::ambiguous_call(m_reporter, file_of(c), c->line, "()"); }
        else                                       { SemanticError::no_matching_overload(m_reporter, file_of(c), c->line, "()"); }
        return nullptr;
    }

    if (is_dynamic_t(callee_type)) {
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (args[i] && !args[i]->is_error() && !args[i]->is_dependent() && rank_conversion(args[i], m_types.dynamic_(), m_types) == ConversionRank::None) report_not_dynamic(args[i], c->m_arguments[i]);
        }
        return m_types.dynamic_();
    }

    return callee_type;
}

bool TypeWalker::derives_from(Scope* s, Scope* base, int depth) {
    if (!s || depth > 32) return false;
    for (Scope* b : s->bases) if (b == base || derives_from(b, base, depth + 1)) return true;
    return false;
}

bool TypeWalker::is_friend_of(Scope* record_scope) {
    if (!record_scope || !record_scope->node || record_scope->node->kind != K::RecordDeclaration) return false;
    auto* rd = static_cast<nodes::RecordDeclaration*>(record_scope->node);
    std::string_view fn_name;
    nodes::OverloadableOperator site_op = nodes::OverloadableOperator::None;

    for (auto it = m_returns.rbegin(); it != m_returns.rend(); ++it) {
        if (!it->site) continue;
        if (it->site->kind == K::FunctionDeclaration) {
            auto* fd = static_cast<nodes::FunctionDeclaration*>(it->site);
            if (fd->m_lowered_operator != nodes::OverloadableOperator::None) site_op = fd->m_lowered_operator;
            else fn_name = fd->get_name();
            break;
        }
        if (it->site->kind == K::OperatorFunctionDeclaration) {
            site_op = static_cast<nodes::OperatorFunctionDeclaration*>(it->site)->get_overload();
            break;
        }
    }

    for (const auto& member : rd->get_members()) {
        nodes::ASTNode* n = member.node;
        if (n && n->kind == K::TemplateDeclaration) n = static_cast<nodes::TemplateDeclaration*>(n)->m_declaration;
        if (!n) continue;
        using RM = modifiers::RawModifiers;

        if (n->kind == K::FunctionDeclaration) {
            auto* f = static_cast<nodes::FunctionDeclaration*>(n);
            if (f->get_modifiers().has(RM::Friend) && !fn_name.empty() && f->get_name() == fn_name) return true;
            if (f->get_modifiers().has(RM::Friend) && site_op != nodes::OverloadableOperator::None && f->m_lowered_operator == site_op) return true;
        } else if (n->kind == K::RecordDeclaration) {
            auto* r = static_cast<nodes::RecordDeclaration*>(n);
            if (!r->get_modifiers().has(RM::Friend)) continue;
            for (Scope* sc = m_current; sc; sc = sc->parent) {
                if (sc->kind == Scope::Kind::Record && sc->owner_symbol && sc->owner_symbol->name == r->get_name()) return true;
            }
        } else if (n->kind == K::OperatorFunctionDeclaration) {
            auto* o = static_cast<nodes::OperatorFunctionDeclaration*>(n);
            if (o->get_modifiers().has(RM::Friend) && site_op != nodes::OverloadableOperator::None && o->get_overload() == site_op) return true;
        }
    }

    return false;
}

void TypeWalker::check_member_access(Symbol* found, nodes::ASTNode* site) {
    if (!found || found->member_access == 0 || !site) return;
    Scope* owner = found->owner;
    if (!owner || owner->kind != Scope::Kind::Record) return;
    const bool is_private = found->member_access == static_cast<std::uint8_t>(nodes::AccessLevel::Private);

    for (Scope* sc = m_current; sc; sc = sc->parent) {
        if (sc->kind != Scope::Kind::Record) continue;
        if (sc == owner) return;
        if (!is_private && derives_from(sc, owner)) return;
    }

    if (is_friend_of(owner)) return;
    std::string_view rec = owner->owner_symbol ? owner->owner_symbol->name : std::string_view{"<record>"};
    SemanticError::inaccessible_member(m_reporter, file_of(site), site->line, found->name, is_private, rec);
}

void TypeWalker::finish_member_access(nodes::MemberAccessExpression* m, bool as_callee) {
    Type* raw = type_member_access(m);
    if (!as_callee && m->resolved && m->resolved->kind == SymbolKind::Function) for (Symbol* o = m->resolved; o; o = o->next_overload) ensure_member_body(o);
    VC vc = symbol_is_object(m->resolved) ? VC::LValue : VC::RValue;
    if (vc == VC::LValue && m->is_dot() && !is_lvalue(m->m_object)) vc = VC::RValue;
    Type* t = decay_ref(raw, vc);
    if (raw && raw->is_reference() && symbol_is_object(m->resolved)) vc = VC::LValue;
    assign_typed(m, t, vc);
}

namespace {

using BMK = parser_types::PrimitiveType::BaseKind;

bool is_text_like(Type* t) {
    return t && t->is_builtin() && (static_cast<BuiltinType*>(t)->base() == BMK::String || static_cast<BuiltinType*>(t)->base() == BMK::Text || static_cast<BuiltinType*>(t)->base() == BMK::StringView);
}

bool is_char_t(Type* t) {
    return t && t->is_builtin() && static_cast<BuiltinType*>(t)->base() == BMK::Char;
}

constexpr std::string_view kAnyMethods[]   = { "sizeof", "alignof", "to_string", "to_text" };
constexpr std::string_view kTextMethods[]  = { "length", "size", "countof", "empty", "substr", "find", "contains", "starts_with", "ends_with", "to_upper", "to_lower", "trim" };
constexpr std::string_view kArrayMethods[] = { "length", "size", "countof", "empty", "find", "contains" };

template <std::size_t N>
bool in_list(const std::string_view (&list)[N], std::string_view name) {
    for (std::string_view s : list) if (s == name) return true;
    return false;
}

} // namespace

std::string TypeWalker::candidate_notes(Symbol* head, const std::vector<Type*>& args, const ArenaVector<nodes::ASTNode*>* arg_nodes, std::string_view shown) {
    if (!head) return {};
    std::vector<std::string> lines;
    std::size_t total = 0;
    auto plural = [](std::size_t n, const char* word) { return std::to_string(n) + " " + word + (n == 1 ? "" : "s"); };

    for (Symbol* o = head; o; o = o->next_overload) {
        if (!o->decl) continue;
        if (o->decl->kind == K::ConstructorDeclaration && static_cast<nodes::ConstructorDeclaration*>(o->decl)->is_deleted()) continue;
        ++total;
        if (lines.size() >= 6) continue;
        Instantiator::Instantiation* owner = m_inst ? m_inst->owning(o) : nullptr;
        EnvGuard g(m_types, owner ? &owner->env : nullptr);
        const std::vector<ParamShape> shapes = shapes_of(o);
        std::string sig = std::string(shown) + "(";

        for (std::size_t i = 0; i < shapes.size(); ++i) {
            if (i) sig += ", ";
            sig += shapes[i].element ? type_str(shapes[i].element) : std::string("?");
            if (shapes[i].is_pack) sig += "...";
            if (shapes[i].has_default) sig += " = ...";
        }

        sig += ")";

        if (o->template_decl) {
            lines.push_back("template " + sig + ": its template arguments can't be worked out from these arguments, or its 'requires' clause isn't met");
            continue;
        }

        std::size_t required = 0;
        bool open_ended = false;
        for (const ParamShape& s : shapes) {
            if (s.is_pack) { if (s.unbounded) open_ended = true; continue; }
            if (!s.has_default) ++required;
        }

        std::string why;
        if (args.size() < required) {
            why = "needs " + std::string(required == shapes.size() ? "" : "at least ") + plural(required, "argument") + ", but " + std::to_string(args.size()) + (args.size() == 1 ? " was" : " were") + " given";
        } else if (!open_ended && args.size() > shapes.size()) {
            why = "takes " + std::string(required == shapes.size() ? "" : "at most ") + plural(shapes.size(), "argument") + ", but " + std::to_string(args.size()) + (args.size() == 1 ? " was" : " were") + " given";
        } else {
            for (std::size_t j = 0; j < args.size() && why.empty(); ++j) {
                const std::size_t k = j < shapes.size() ? j : shapes.size() - 1;
                if (shapes.empty() || !args[j] || !shapes[k].element || args[j]->is_error()) continue;
                Type* want = shapes[k].element;
                if (is_dynamic_t(want) || want->is_dependent()) continue;
                if (rank_conversion(args[j], want, m_types) == ConversionRank::None) {
                    why = "argument " + std::to_string(j + 1) + " is '" + type_str(args[j]) + "', which can't be converted to '" + type_str(want) + "'";
                    continue;
                }
                if (want->is_reference() && arg_nodes && j < arg_nodes->size()) {
                    auto* rt = static_cast<ReferenceType*>(want);
                    const bool lv = is_lvalue((*arg_nodes)[j]);
                    if (rt->ref_qual() == RefQual::LValue && !rt->referent()->cv().is_const && !lv) why = "argument " + std::to_string(j + 1) + " is a temporary value, and a temporary can't be passed to '" + type_str(want) + "' (make the parameter 'const " + type_str(rt->referent()) + "&', or store the value in a variable first)";
                    else if (rt->ref_qual() == RefQual::RValue && lv) why = "argument " + std::to_string(j + 1) + " is a variable, and '" + type_str(want) + "' only takes temporaries (pass 'std::core::move(...)' to give it away)";
                    else if (rt->ref_qual() == RefQual::LValue && !rt->referent()->cv().is_const && args[j]->cv().is_const) why = "argument " + std::to_string(j + 1) + " is const, but '" + type_str(want) + "' could change it";
                }
            }
        }

        lines.push_back(sig + (why.empty() ? std::string(": doesn't accept these arguments") : ": " + why));
    }

    if (lines.empty()) return {};
    std::string given = "(";
    for (std::size_t j = 0; j < args.size(); ++j) given += (j ? ", " : "") + (args[j] ? type_str(args[j]) : std::string("?"));
    given += ")";
    std::string out = " with arguments " + given + "\n" + (total == 1 ? "The candidate is:" : "The candidates are:");
    for (const std::string& l : lines) out += "\n  - " + l;
    if (total > lines.size()) out += "\n  - ... and " + std::to_string(total - lines.size()) + " more";
    return out;
}

bool TypeWalker::rewrite_extension_call(nodes::CallExpression* c) {
    auto* m = static_cast<nodes::MemberAccessExpression*>(c->m_callee);
    if (!m->qualifier.empty() || m->is_arrow() || !m_inst) return false;
    Type* obj = type_of(m->m_object);
    if (!obj || obj->is_error()) return false;
    if (obj->is_reference()) obj = static_cast<ReferenceType*>(obj)->referent();
    obj = m_types.strip_cv(obj);
    if (!obj || obj->is_dependent() || !(is_text_like(obj) || is_char_t(obj))) return false;
    const std::string_view name = m->get_member();
    if (name == "sizeof" || name == "alignof" || name == "countof" || in_list(kAnyMethods, name)) return false;
    if (is_text_like(obj) && in_list(kTextMethods, name)) return false;
    const std::vector<std::string_view> parts{ "std", "strings", name };
    Symbol* fn = nullptr;
    for (Scope* s = m_current; s && !fn; s = s->parent) {
        Symbol* found = descend(s, parts, 0);
        if (found && found->kind == SymbolKind::Function) fn = found;
    }
    if (!fn) return false;
    auto* id = make_in<nodes::Identifier>(m_inst->arena(), fn->name, c->line);
    id->resolved = fn;
    id->file_id = c->file_id;
    id->order = c->order;
    c->m_arguments.insert(c->m_arguments.begin(), m->m_object);
    c->m_callee = id;
    return true;
}

bool TypeWalker::is_builtin_method_call(nodes::CallExpression* c) {
    auto* m = static_cast<nodes::MemberAccessExpression*>(c->m_callee);
    if (!m->qualifier.empty()) return false;
    Type* obj = type_of(m->m_object);
    if (!obj || obj->is_error()) return false;
    if (obj->is_reference()) obj = static_cast<ReferenceType*>(obj)->referent();
    if (m->is_arrow()) {
        if (!obj->is_pointer()) return false;
        obj = static_cast<PointerType*>(obj)->pointee();
    }
    obj = m_types.strip_cv(obj);
    if (!obj || obj->is_dependent() || is_dynamic_t(obj)) return false;

    const std::string_view name = m->get_member();
    if (name == "sizeof" || name == "alignof" || name == "countof") return true;
    if (obj->is_record()) return false;
    if (in_list(kAnyMethods, name)) return true;
    if (obj->is_enum() && name == "name") return true;
    if (is_text_like(obj)) return in_list(kTextMethods, name);
    if (obj->is_array()) return in_list(kArrayMethods, name);
    if (obj->is_builtin() || obj->is_enum() || obj->is_pointer()) {
        const bool stringy = is_text_like(obj) || is_char_t(obj);
        const std::string hint = stringy ? "; more methods for strings and characters, such as split, replace, join, pad_left and is_digit, come from 'import module.strings'" : std::string();
        SemanticError::emit(m_reporter, file_of(m), m->line, "Type", "'" + type_str(obj) + "' has no built-in method '" + std::string(name) + "'" + hint);
        c->builtin_method = name;
        return true;
    }
    return false;
}

Type* TypeWalker::type_builtin_method(nodes::CallExpression* c, const std::vector<Type*>& args) {
    auto* m = static_cast<nodes::MemberAccessExpression*>(c->m_callee);
    if (!c->builtin_method.empty()) return m_types.error_();

    Type* obj = type_of(m->m_object);
    if (obj->is_reference()) obj = static_cast<ReferenceType*>(obj)->referent();
    if (m->is_arrow()) obj = static_cast<PointerType*>(obj)->pointee();
    Type* plain = m_types.strip_cv(obj);
    const std::string_view name = m->get_member();
    c->builtin_method = name;

    const std::uint32_t file = file_of(c);
    const std::string where = "'" + type_str(plain) + "." + std::string(name) + "()'";
    Type* int_t  = m_types.builtin(BMK::Int);
    Type* bool_t = m_types.builtin(BMK::Bool);

    auto arity = [&](std::size_t lo, std::size_t hi) {
        if (args.size() >= lo && args.size() <= hi) return true;
        std::string want = lo == hi ? std::to_string(lo) : std::to_string(lo) + " or " + std::to_string(hi);
        SemanticError::emit(m_reporter, file, c->line, "Call", where + " takes " + want + " argument" + (hi == 1 && lo == hi ? "" : "s") + ", but " + std::to_string(args.size()) + (args.size() == 1 ? " was" : " were") + " given");
        return false;
    };

    auto arg_plain = [&](std::size_t k) {
        Type* t = args[k];
        if (t && t->is_reference()) t = static_cast<ReferenceType*>(t)->referent();
        return m_types.strip_cv(t);
    };

    auto need_integral = [&](std::size_t k) {
        Type* t = arg_plain(k);
        if (known_t(t) && !is_int_type(t) && !is_dynamic_t(t)) SemanticError::emit(m_reporter, file, c->line, "Type", "argument " + std::to_string(k + 1) + " of " + where + " must be an integer, not '" + type_str(t) + "'");
    };

    auto need_text = [&](std::size_t k) {
        Type* t = arg_plain(k);
        if (known_t(t) && !is_text_like(t) && !is_char_t(t) && !is_dynamic_t(t)) SemanticError::emit(m_reporter, file, c->line, "Type", "argument " + std::to_string(k + 1) + " of " + where + " must be a string, text or char, not '" + type_str(t) + "'");
    };

    if (name == "to_string") { arity(0, 0); return m_types.builtin(BMK::String); }
    if (name == "name" && plain->is_enum()) { arity(0, 0); return m_types.builtin(BMK::StringView); }
    if (name == "to_text")   { arity(0, 0); return m_types.builtin(BMK::Text); }

    if (name == "sizeof" || name == "alignof") {
        arity(0, 0);
        long long size = 0, alignment = 0;
        if (plain->is_dependent()) return int_t;
        if (m_eval.layout_of(plain, size, alignment)) {
            c->builtin_size = name == "sizeof" ? size : alignment;
            return int_t;
        }
        if (name == "sizeof" && is_text_like(plain)) return int_t;
        if (name == "sizeof" && plain->is_array()) {
            Type* el = m_types.strip_cv(static_cast<ArrayType*>(plain)->element());
            if (m_eval.layout_of(el, size, alignment)) { c->builtin_size = size; c->builtin_method = "sizeof[]"; return int_t; }
        }
        SemanticError::emit(m_reporter, file, c->line, "Type", where + " is not defined because '" + type_str(plain) + "' has no fixed size");
        return int_t;
    }

    if (name == "countof" && !is_text_like(plain) && !plain->is_array()) {
        arity(0, 0);
        if (plain->is_enum()) {
            if (std::optional<std::size_t> n = enumerator_count(plain)) c->builtin_size = static_cast<long long>(*n);
            return int_t;
        }
        SemanticError::emit(m_reporter, file, c->line, "Type", "countof requires an array, a string or an enum, not '" + type_str(plain) + "'");
        return int_t;
    }

    if (name == "length" || name == "size" || name == "countof") { arity(0, 0); return int_t; }
    if (name == "empty") { arity(0, 0); return bool_t; }

    if (is_text_like(plain)) {
        Type* same = owning_string(m_types.builtin(static_cast<BuiltinType*>(plain)->base()));
        if (name == "substr") { if (arity(1, 2)) for (std::size_t k = 0; k < args.size(); ++k) need_integral(k); return same; }
        if (name == "find")   { if (arity(1, 1)) need_text(0); return int_t; }
        if (name == "contains" || name == "starts_with" || name == "ends_with") { if (arity(1, 1)) need_text(0); return bool_t; }
        arity(0, 0);
        return same;
    }

    if (arity(1, 1)) {
        Type* el = static_cast<ArrayType*>(plain)->element();
        Type* a  = arg_plain(0);
        if (known_t(a) && known_t(el) && rank_conversion(a, m_types.strip_cv(el), m_types) == ConversionRank::None) {
            SemanticError::emit(m_reporter, file, c->line, "Type", "argument 1 of " + where + " must be convertible to '" + type_str(el) + "', not '" + type_str(a) + "'");
        }
    }
    return name == "find" ? int_t : bool_t;
}

Type* TypeWalker::type_member_access(nodes::MemberAccessExpression* m) {
    if (m->is_scope()) {
        if (m->m_object && m->m_object->kind == K::TemplateInstantiation && m_inst) {
            Type* ot = m_types.strip_cv(type_of(m->m_object));
            if (ot && ot->is_record() && static_cast<RecordType*>(ot)->is_instantiation()) {
                if (Instantiator::Instantiation* I = m_inst->for_record(static_cast<RecordType*>(ot), m)) {
                    ensure_typed(I);
                    if (I->sym && I->sym->inner_scope) m->resolved = I->sym->inner_scope->find_member(m->get_member());
                    if (!m->resolved) SemanticError::unresolved_name(m_reporter, file_of(m), m->line, m->get_member());
                }
            }
        }
        check_member_access(m->resolved, m);
        return symbol_type(m->resolved);
    }
    Type* obj = type_of(m->m_object);
    if (!obj) return nullptr;
    m->arrow_ops.clear();
    if (m->is_arrow()) obj = arrow_target(obj, &m->arrow_ops);
    Scope* members = nullptr;
    Instantiator::Instantiation* I = nullptr;
    Type* s = m_types.strip_cv(obj);
    if (s && s->is_reference()) s = m_types.strip_cv(static_cast<ReferenceType*>(s)->referent());

    if (s && s->is_record()) {
        auto* rt = static_cast<RecordType*>(s);

        if (rt->is_instantiation() && m_inst) {
            I = m_inst->for_record(rt, m);              
            if (I && I->sym) members = I->sym->inner_scope;
        }

        if (!members && rt->decl()) members = rt->decl()->inner_scope;
    }

    if (!m->qualifier.empty()) {
        Scope* via = (members && m->qualifier_symbol) ? qualified_base_scope(members, m->qualifier_symbol, 0) : nullptr;

        if (!via) {
            if (obj && !obj->is_error() && m->qualifier_symbol) {
                std::string q;
                for (std::string_view p : m->qualifier) { if (!q.empty()) q += "::"; q += p; }
                SemanticError::emit(m_reporter, file_of(m), m->line, "Name", "'" + q + "' isn't '" + type_str(s) + "' or one of its base classes, so '" + q + "::" + std::string(m->get_member()) + "' can't be used on it");
            }
            return nullptr;
        }

        m->qualifier_scope = via;
        members = via;
    }

    Symbol* found = members ? members->find_member(m->get_member()) : nullptr;
    if (found) check_member_ambiguity(members, m->get_member(), m);
    m->resolved = found;
    check_member_access(found, m);
        
    if (!found) {
        if (obj && !is_dynamic_t(obj) && !obj->is_error()) SemanticError::emit(m_reporter, file_of(m), m->line, "Name", "'" + type_str(s) + "' has no member named '" + std::string(m->get_member()) + "'" + suggest_similar(members, m->get_member(), true));
        return nullptr;
    }

    EnvGuard g(m_types, I ? &I->env : nullptr);          
    Type* ft = symbol_type(found);
    Type* ov = obj && obj->is_reference() ? static_cast<ReferenceType*>(obj)->referent() : obj;
    if (ov && ov->cv().is_const && is_instance_field(found)) ft = add_const(ft);
    if (lazy_blocked(found, ov && ov->cv().is_const)) ft = add_const(ft);
    return ft;
}

std::size_t TypeWalker::fixed_parameter_count(const Symbol* fn) {
    if (!fn || !fn->decl) return 0;
    const nodes::FunctionParameters* ps = nullptr;
    if (fn->decl->kind == K::FunctionDeclaration) ps = static_cast<const nodes::FunctionDeclaration*>(fn->decl)->get_parameters();
    else if (fn->decl->kind == K::OperatorFunctionDeclaration) ps = static_cast<const nodes::OperatorFunctionDeclaration*>(fn->decl)->get_parameters();
    else if (fn->decl->kind == K::ConstructorDeclaration) ps = static_cast<const nodes::ConstructorDeclaration*>(fn->decl)->get_parameters();
    if (!ps) return 1;
    std::size_t n = 0;
    bool variadic = false;
    for (const nodes::FunctionParameter* p : ps->m_params) {
        if (!p->is_variadic() && !p->expanded_pack) ++n;
        else variadic = true;
    }
    return n * 2 + (variadic ? 0 : 1);
}

nodes::VariableDeclaration* TypeWalker::pack_field_group_of(Symbol* s) {
    if (!s || !s->bound_type || !s->decl || s->decl->kind != K::VariableDeclaration) return nullptr;
    auto* v = static_cast<nodes::VariableDeclaration*>(s->decl);
    return v->pack_field && v->symbol == s ? v : nullptr;
}

nodes::VariableDeclaration* TypeWalker::pack_field_group(nodes::ASTNode* e) {
    if (!e) return nullptr;
    if (e->kind == K::Identifier) return pack_field_group_of(static_cast<nodes::Identifier*>(e)->resolved);
    if (e->kind == K::MemberAccessExpression) return pack_field_group_of(static_cast<nodes::MemberAccessExpression*>(e)->resolved);
    return nullptr;
}

bool TypeWalker::defer_member_body(nodes::ASTNode* node, Symbol* sym, bool has_body, bool eager) {
    const Symbol* owner = sym && sym->owner ? sym->owner->owner_symbol : nullptr;
    const bool native = owner && owner->decl && owner->decl->kind == K::RecordDeclaration && static_cast<nodes::RecordDeclaration*>(owner->decl)->m_native;
    auto used_flag = [](nodes::ASTNode* n) -> bool* {
        if (n && n->kind == K::FunctionDeclaration) return &static_cast<nodes::FunctionDeclaration*>(n)->native_used;
        if (n && n->kind == K::OperatorFunctionDeclaration) return &static_cast<nodes::OperatorFunctionDeclaration*>(n)->native_used;
        return nullptr;
    };
    bool* node_used = used_flag(node);
    bool* sym_used  = sym ? used_flag(sym->decl) : nullptr;
    if ((node_used && *node_used) || (sym_used && *sym_used)) {
        if (node_used) *node_used = true;
        return false;
    }
    if (m_policy && m_policy->eager_members) eager = true;
    if (eager || !(has_body || native) || !sym || !m_inst || node == m_force_node) return false;
    if (!is_member_function(sym) || m_requested.count(sym)) return false;
    Instantiator::Instantiation* I = m_inst->owning(sym);
    if (!I || !I->decl || I->decl->kind != K::RecordDeclaration) return false;
    m_deferred[sym] = DeferredBody{ node, m_current, I };
    return true;
}

void TypeWalker::ensure_member_body(Symbol* sym) {
    if (!sym) return;
    if (sym->decl && sym->decl->kind == K::FunctionDeclaration) static_cast<nodes::FunctionDeclaration*>(sym->decl)->native_used = true;
    if (sym->decl && sym->decl->kind == K::OperatorFunctionDeclaration) static_cast<nodes::OperatorFunctionDeclaration*>(sym->decl)->native_used = true;
    auto it = m_deferred.find(sym);
    if (it == m_deferred.end()) {
        if (m_inst && is_member_function(sym)) m_requested.insert(sym);
        return;
    }
    const DeferredBody d = it->second;
    m_deferred.erase(it);
    if (d.node->kind == K::FunctionDeclaration) static_cast<nodes::FunctionDeclaration*>(d.node)->lazy_untyped = false;
    if (d.node->kind == K::OperatorFunctionDeclaration) static_cast<nodes::OperatorFunctionDeclaration*>(d.node)->lazy_untyped = false;
    if (d.inst) m_types.push_subst(&d.inst->env);
    Scope* saved = m_current;
    const bool saved_const = m_in_const_method;
    nodes::ASTNode* saved_force = m_force_node;
    const auto saved_catch = m_catch_depth;
    nodes::ASTNode* saved_site = m_site;
    m_current = d.scope;
    m_force_node = d.node;
    m_catch_depth = 0;
    build(d.node);
    m_site = saved_site;
    m_catch_depth = saved_catch;
    m_force_node = saved_force;
    m_in_const_method = saved_const;
    m_current = saved;
    if (d.inst) m_types.pop_subst();
}

int TypeWalker::call_object_constness(nodes::CallExpression* c) {
    if (!c || !c->m_callee) return -1;
    if (c->m_callee->kind == K::Identifier) return m_in_const_method ? 1 : 0;
    if (c->m_callee->kind != K::MemberAccessExpression) return -1;
    auto* m = static_cast<nodes::MemberAccessExpression*>(c->m_callee);
    if (m->is_scope()) return m_in_const_method ? 1 : 0;
    Type* o = type_of(m->m_object);
    if (o && o->is_reference()) o = static_cast<ReferenceType*>(o)->referent();
    if (o && m->is_arrow() && o->is_pointer()) o = static_cast<PointerType*>(o)->pointee();
    return o && o->cv().is_const ? 1 : 0;
}

Type* TypeWalker::init_list_element(Type* t) {
    if (t && t->is_reference()) t = static_cast<ReferenceType*>(t)->referent();
    t = m_types.strip_cv(t);
    if (!t || !t->is_record()) return nullptr;
    auto* rt = static_cast<RecordType*>(t);
    Symbol* d = rt->decl();
    if (!d || d->name != "initializer_list" || rt->args().size() != 1 || !rt->args()[0].is_type) return nullptr;
    if (!d->owner || !d->owner->owner_symbol || d->owner->owner_symbol->name != "std") return nullptr;
    return rt->args()[0].type;
}

bool TypeWalker::element_converts(nodes::ASTNode* el, Type* to) {
    if (!el || !to) return false;
    if (el->kind == K::BraceInitializerList) return rank_brace_arg(el, to) != ConversionRank::None;
    Type* from = type_of(el);
    if (!from) return false;
    return rank_conversion(from, to, m_types) != ConversionRank::None || constant_fits(el, from, to);
}

Symbol* TypeWalker::init_list_constructor(Type* rec, Span<nodes::ASTNode* const> els, Type*& il_type, nodes::ASTNode* site) {
    il_type = nullptr;
    Type* t = m_types.strip_cv(rec);
    if (!t || !t->is_record() || els.empty() || init_list_element(t)) return nullptr;
    auto* rt = static_cast<RecordType*>(t);
    Symbol* owner = rt->decl();
    Instantiator::Instantiation* I = nullptr;

    if (rt->is_instantiation() && m_inst) {
        I = m_inst->for_record(rt, site);
        if (!I || !I->sym) return nullptr;
        ensure_typed(I);
        owner = I->sym;
    }

    if (!owner || !owner->inner_scope) return nullptr;
    EnvGuard g(m_types, I ? &I->env : nullptr);

    for (Symbol* o = owner->inner_scope->find_member("constructor"); o; o = o->next_overload) {
        if (o->template_decl || !o->decl || o->decl->kind != K::ConstructorDeclaration) continue;
        if (static_cast<nodes::ConstructorDeclaration*>(o->decl)->is_deleted()) continue;
        const std::vector<ParamShape> shapes = shapes_of(o);
        if (shapes.size() != 1 || shapes[0].is_pack) continue;
        Type* p = shapes[0].element;
        if (p && p->is_reference() && static_cast<ReferenceType*>(p)->ref_qual() == RefQual::LValue && !static_cast<ReferenceType*>(p)->referent()->cv().is_const) continue;
        Type* e = init_list_element(p);
        if (!e) continue;
        bool all = true;
        for (nodes::ASTNode* el : els) all = all && element_converts(el, e);
        if (!all) continue;
        Type* lt = p->is_reference() ? static_cast<ReferenceType*>(p)->referent() : p;
        il_type = m_types.strip_cv(lt);
        return o;
    }

    return nullptr;
}

ConversionRank TypeWalker::rank_brace_arg(nodes::ASTNode* arg, Type* param) {
    if (!arg || arg->kind != K::BraceInitializerList || !param) return ConversionRank::None;
    auto* list = static_cast<nodes::BraceInitializerList*>(arg);
    Type* p = param;

    if (p->is_reference()) {
        auto* r = static_cast<ReferenceType*>(p);
        if (r->ref_qual() == RefQual::LValue && !r->referent()->cv().is_const) return ConversionRank::None;
        p = r->referent();
    }

    p = m_types.strip_cv(p);
    if (!p || p->is_error() || p->is_dependent()) return ConversionRank::None;

    if (Type* e = init_list_element(p)) {
        for (nodes::ASTNode* el : list->m_elements) if (!element_converts(el, e)) return ConversionRank::None;
        return ConversionRank::Exact;
    }

    if (p->is_record()) {
        Type* il = nullptr;
        if (init_list_constructor(p, list->m_elements, il, arg)) return ConversionRank::UserDefined;
        std::vector<Type*> els;
        for (nodes::ASTNode* el : list->m_elements) {
            Type* et = el->kind == K::BraceInitializerList ? nullptr : type_of(el);
            if (!et && el->kind != K::BraceInitializerList) return ConversionRank::None;
            els.push_back(et);
        }
        auto* rt = static_cast<RecordType*>(p);
        Symbol* owner = rt->decl();
        Instantiator::Instantiation* I = nullptr;
        if (rt->is_instantiation() && m_inst) {
            I = m_inst->for_record(rt, arg);
            if (!I || !I->sym) return ConversionRank::None;
            ensure_typed(I);
            owner = I->sym;
        }
        if (!owner || !owner->inner_scope) return ConversionRank::None;
        EnvGuard g(m_types, I ? &I->env : nullptr);
        Symbol* bucket = owner->inner_scope->find_member("constructor");
        if (!bucket) return ConversionRank::UserDefined;
        for (Symbol* o = bucket; o; o = o->next_overload) {
            if (o->template_decl || !o->decl || o->decl->kind != K::ConstructorDeclaration) continue;
            if (static_cast<nodes::ConstructorDeclaration*>(o->decl)->is_deleted()) continue;
            if (static_cast<nodes::ConstructorDeclaration*>(o->decl)->m_qualifiers.has(modifiers::FunctionQualifiers::Explicit)) continue;
            if (match_arguments(shapes_of(o), els, m_types).viable) return ConversionRank::UserDefined;
        }
        return ConversionRank::None;
    }

    if (list->m_elements.empty()) return ConversionRank::Exact;
    if (list->m_elements.size() != 1 || list->m_elements[0]->kind == K::BraceInitializerList) return ConversionRank::None;
    Type* et = type_of(list->m_elements[0]);
    if (!et) return ConversionRank::None;
    if (constant_fits(list->m_elements[0], et, p)) return ConversionRank::Exact;
    return rank_conversion(et, p, m_types);
}

void TypeWalker::bind_brace_args(const ArenaVector<nodes::ASTNode*>* nodes, const std::vector<ParamShape>& shapes, nodes::ASTNode* site) {
    if (!nodes) return;
    std::size_t i = 0;
    for (const ParamShape& p : shapes) {
        if (p.is_pack || i >= nodes->size()) break;
        nodes::ASTNode* a = (*nodes)[i++];
        if (a && a->kind == K::BraceInitializerList) check_brace_init(static_cast<nodes::BraceInitializerList*>(a), p.element, site);
    }
}

void TypeWalker::check_brace_init(nodes::BraceInitializerList* list, Type* target, nodes::ASTNode* site) {
    Type* t = target;
    if (t && t->is_reference()) t = static_cast<ReferenceType*>(t)->referent();
    t = m_types.strip_cv(t);
    if (!t || t->is_dependent() || t->is_error() || t->is_array() || is_dynamic_t(t)) return;

    if (record_symbol_of(t)) {
        Type* il = nullptr;
        if (Symbol* via = list->parens ? nullptr : init_list_constructor(t, list->m_elements, il, site)) {
            Type* e = init_list_element(il);
            std::vector<Type*> args;
            for (nodes::ASTNode* el : list->m_elements) {
                if (el->kind == K::BraceInitializerList) {
                    check_brace_init(static_cast<nodes::BraceInitializerList*>(el), e, site);
                    args.push_back(e);
                } else {
                    args.push_back(type_of(el));
                }
            }
            list->ctor = via;
            list->init_list_type = il;
            m_ctor_arg_nodes = &list->m_elements;
            list->init_list_ctor = resolve_construction(il, args, site);
            m_ctor_arg_nodes = nullptr;
            return;
        }

        Scope* ms = member_scope_of(t);
        if (!ms || !ms->find_member("constructor")) return;
        std::vector<Type*> args;
        for (nodes::ASTNode* el : list->m_elements) {
            Type* at = type_of(el);
            if (!at) return;
            args.push_back(at);
        }
        m_ctor_arg_nodes = &list->m_elements;
        list->ctor = resolve_construction(t, args, site);
        m_ctor_arg_nodes = nullptr;
        return;
    }

    if (list->m_elements.size() > 1) {
        SemanticError::emit(m_reporter, file_of(site), site->line, "Initialization", "'" + type_str(t) + "' holds a single value, so its initializer can have at most one, but " + std::to_string(list->m_elements.size()) + " were given");
        return;
    }

    if (list->m_elements.size() == 1) {
        nodes::ASTNode* el = list->m_elements.front();
        if (Type* et = type_of(el)) check_binding(el, et, t, site);
    }
}

Scope* TypeWalker::qualified_base_scope(Scope* rs, Symbol* q, int depth) {
    if (!rs || depth > 32) return nullptr;
    Symbol* owner = rs->owner_symbol;
    if (owner && (owner == q || (m_types.instance_primary_hook && m_types.instance_primary_hook(owner) == q))) return rs;
    for (Scope* b : rs->bases) if (Scope* f = qualified_base_scope(b, q, depth + 1)) return f;
    return nullptr;
}

bool TypeWalker::in_friend_body() const {
    for (auto it = m_returns.rbegin(); it != m_returns.rend(); ++it) {
        if (!it->site) continue;
        if (it->site->kind == K::FunctionDeclaration) return static_cast<const nodes::FunctionDeclaration*>(it->site)->hoisted_friend;
        if (it->site->kind == K::OperatorFunctionDeclaration) return static_cast<const nodes::OperatorFunctionDeclaration*>(it->site)->hoisted_friend;
        if (it->site->kind == K::ConstructorDeclaration || it->site->kind == K::DestructorDeclaration) return false;
    }
    return false;
}

bool TypeWalker::is_static_member_function(const Symbol* fn) {
    if (!fn || !fn->decl) return false;
    if (fn->decl->kind == K::FunctionDeclaration) return static_cast<const nodes::FunctionDeclaration*>(fn->decl)->get_modifiers().has(modifiers::RawModifiers::Static);
    return false;
}

bool TypeWalker::is_lazy_field(const Symbol* s) {
    if (!s || s->kind != SymbolKind::Variable || !s->owner || s->owner->kind != Scope::Kind::Record || !s->decl) return false;
    return s->decl->kind == K::VariableDeclaration && static_cast<const nodes::VariableDeclaration*>(s->decl)->get_type_info().modifiers.has(modifiers::RawModifiers::Lazy);
}

bool TypeWalker::lazy_blocked(const Symbol* s, bool object_const) {
    if (!is_lazy_field(s)) return false;
    for (Scope* sc = m_current; sc; sc = sc->parent) if (sc->kind == Scope::Kind::Record && sc == s->owner) return false;
    if (is_friend_of(s->owner)) return false;
    if (!static_cast<const nodes::VariableDeclaration*>(s->decl)->get_type_info().modifiers.lazy_public()) return true;
    return object_const;
}

std::string TypeWalker::lazy_write_problem(nodes::ASTNode* target) {
    Symbol* s = nullptr;
    bool object_const = false;

    if (target && target->kind == K::Identifier) {
        s = static_cast<nodes::Identifier*>(target)->resolved;
    } else if (target && target->kind == K::MemberAccessExpression) {
        auto* m = static_cast<nodes::MemberAccessExpression*>(target);
        if (m->is_scope()) return {};
        s = m->resolved;
        Type* ot = type_of(m->m_object);
        if (ot && ot->is_reference()) ot = static_cast<ReferenceType*>(ot)->referent();
        if (m->is_arrow() && ot && ot->is_pointer()) ot = static_cast<PointerType*>(ot)->pointee();
        object_const = ot && ot->cv().is_const;
    }

    if (!lazy_blocked(s, object_const)) return {};
    std::string_view rec = s->owner && s->owner->owner_symbol ? s->owner->owner_symbol->name : std::string_view{"its class"};
    if (!static_cast<const nodes::VariableDeclaration*>(s->decl)->get_type_info().modifiers.lazy_public()) {
        return "'" + std::string(s->name) + "' is a lazy field, so only '" + std::string(rec) + "' can assign or invalidate it; declare it 'public lazy' to allow that from outside the class";
    }
    return "'" + std::string(s->name) + "' belongs to a const '" + std::string(rec) + "', so it can't be assigned or invalidated here";
}

bool TypeWalker::is_instance_field(const Symbol* s) {
    if (!s || s->kind != SymbolKind::Variable || !s->owner || s->owner->kind != Scope::Kind::Record || !s->decl) return false;
    using RM = modifiers::RawModifiers;
    if (s->decl->kind == K::VariableDeclaration) {
        const auto& mods = static_cast<const nodes::VariableDeclaration*>(s->decl)->get_type_info().modifiers;
        return !mods.has(RM::Static) && !mods.has(RM::Mutable) && !mods.has(RM::Lazy);
    }
    if (s->decl->kind == K::ArrayDeclaration) {
        const auto& mods = static_cast<const nodes::ArrayDeclaration*>(s->decl)->get_array_modifiers();
        return !mods.has(RM::Static) && !mods.has(RM::Mutable);
    }
    return false;
}

bool TypeWalker::is_object_member(const Symbol* s) {
    if (!s || !s->owner || !s->decl) return false;
    const Scope* owner = s->owner;
    while (owner && owner->kind == Scope::Kind::Template) owner = owner->parent;
    if (!owner || owner->kind != Scope::Kind::Record) return false;
    using RM = modifiers::RawModifiers;
    if (s->kind == SymbolKind::Variable) {
        if (s->decl->kind == K::VariableDeclaration) return !static_cast<const nodes::VariableDeclaration*>(s->decl)->get_type_info().modifiers.has(RM::Static);
        if (s->decl->kind == K::ArrayDeclaration) return !static_cast<const nodes::ArrayDeclaration*>(s->decl)->get_array_modifiers().has(RM::Static);
        return false;
    }
    if (s->kind == SymbolKind::Function) {
        for (const Symbol* o = s; o; o = o->next_overload) {
            if (!o->decl) continue;
            if (o->decl->kind == K::OperatorFunctionDeclaration) return true;
            if (o->decl->kind == K::FunctionDeclaration && !static_cast<const nodes::FunctionDeclaration*>(o->decl)->get_modifiers().has(RM::Static)
                && !static_cast<const nodes::FunctionDeclaration*>(o->decl)->hoisted_friend) return true;
        }
    }
    return false;
}

void TypeWalker::mark_safe_subtree(nodes::ASTNode* n) {
    mark_safe_subtree(n, nodes::SafetySafe);
}

void TypeWalker::mark_safe_subtree(nodes::ASTNode* n, std::uint8_t level) {
    if (!n) return;
    n->safety = level;
    nodes::for_each_child(n, [level](nodes::ASTNode* c) { mark_safe_subtree(c, level); });
}

TypeWalker::SelfRules TypeWalker::enter_self_rules(nodes::ASTNode* fn, Symbol* sym, nodes::FunctionParameters* params, bool special) {
    SelfRules saved{ m_explicit_self, m_self_by_value, m_receiver_this };
    m_explicit_self = false;
    m_self_by_value = false;
    m_receiver_this = nullptr;

    const Scope* owner = sym ? sym->owner : nullptr;
    while (owner && owner->kind == Scope::Kind::Template) owner = owner->parent;
    nodes::RecordDeclaration* rec = owner && owner->kind == Scope::Kind::Record && owner->node && owner->node->kind == K::RecordDeclaration
                                  ? static_cast<nodes::RecordDeclaration*>(owner->node) : nullptr;
    const bool hoisted = (fn->kind == K::FunctionDeclaration && static_cast<nodes::FunctionDeclaration*>(fn)->hoisted_friend)
                      || (fn->kind == K::OperatorFunctionDeclaration && static_cast<nodes::OperatorFunctionDeclaration*>(fn)->hoisted_friend);
    const bool member = rec && !hoisted;
    if (member && nodes::in_safe_code(rec) && fn->safety == nodes::SafetyNone) mark_safe_subtree(fn, rec->safety);

    const bool is_static = fn->kind == K::FunctionDeclaration && static_cast<nodes::FunctionDeclaration*>(fn)->get_modifiers().has(modifiers::RawModifiers::Static);
    const bool has_receiver = params && params->has_receiver();
    const std::uint32_t at = params && params->receiver_line ? params->receiver_line : fn->line;

    if (has_receiver) {
        if (!member) {
            safety_error(fn, at, "only a method takes a 'self' receiver; '" + std::string(sym ? sym->name : std::string_view("this function")) + "' isn't part of a class, so take the object as an ordinary parameter");
        } else if (is_static) {
            safety_error(fn, at, "a static method isn't called on an object, so it can't take a 'self' receiver; remove 'static' or the receiver");
        } else {
            m_explicit_self = true;
            m_self_by_value = params->receiver_by_value();
        }
    }

    if (member && !rec->m_native && (nodes::in_safe_code(fn) || nodes::in_safe_code(rec))) {
        m_explicit_self = true;
        if (!special && !has_receiver && !is_static) {
            const std::string name = sym ? std::string(sym->name) : std::string("this method");
            safety_error(fn, fn->line, "in safe code a method says how it uses its object: add a receiver as the first parameter of '" + name + "', as in '" + name + "(const self&)' to read it, '" + name + "(self&)' to change it, or '" + name + "(self)' to work on a copy; or make it 'static'");
        }
    }

    if (m_self_by_value && fn->kind != K::ConstructorDeclaration) {
        nodes::ASTNode* body = nullptr;
        if (fn->kind == K::FunctionDeclaration) body = static_cast<nodes::FunctionDeclaration*>(fn)->get_body();
        if (fn->kind == K::OperatorFunctionDeclaration) body = static_cast<nodes::OperatorFunctionDeclaration*>(fn)->m_body;
        if (body && body->kind == K::BlockStatement) {
            auto* b = static_cast<nodes::BlockStatement*>(body);
            if (!b->statements.empty() && b->statements[0] && b->statements[0]->kind == K::VariableDeclaration) {
                auto* v = static_cast<nodes::VariableDeclaration*>(b->statements[0]);
                if (v->receiver_copy && v->m_initializer && v->m_initializer->kind == K::DereferenceExpression) m_receiver_this = static_cast<nodes::DereferenceExpression*>(v->m_initializer)->operand;
            }
        }
    }

    return saved;
}

void TypeWalker::restore_self_rules(const SelfRules& saved) {
    m_explicit_self = saved.explicit_self;
    m_self_by_value = saved.by_value;
    m_receiver_this = saved.receiver_this;
}

void TypeWalker::check_bare_member(nodes::Identifier* id) {
    if (!m_explicit_self || !id || !id->resolved || in_friend_body()) return;
    if (!is_object_member(id->resolved)) return;
    const std::string name(id->name);
    if (id->resolved->kind == SymbolKind::Function) {
        safety_error(id, id->line, "'" + name + "' is a method of this object, and this method names its object explicitly; call it as 'self." + name + "(...)'");
    } else {
        safety_error(id, id->line, "'" + name + "' is a field of this object, and this method names its object explicitly; write 'self." + name + "'");
    }
}

void TypeWalker::check_template_receivers(nodes::ASTNode* decl) {
    if (!decl || decl->kind != K::RecordDeclaration) return;
    auto* rec = static_cast<nodes::RecordDeclaration*>(decl);
    if (!nodes::in_safe_code(rec) || rec->m_native) return;

    for (const nodes::RecordDeclaration::Member& m : rec->get_members()) {
        nodes::ASTNode* n = m.node;
        if (n && n->kind == K::TemplateDeclaration) n = static_cast<nodes::TemplateDeclaration*>(n)->m_declaration;
        if (!n) continue;

        if (n->kind == K::RecordDeclaration) {
            check_template_receivers(n);
        } else if (n->kind == K::FunctionDeclaration) {
            auto* fd = static_cast<nodes::FunctionDeclaration*>(n);
            if (fd->hoisted_friend || fd->get_modifiers().has(modifiers::RawModifiers::Static)) continue;
            if (fd->get_parameters() && fd->get_parameters()->has_receiver()) continue;
            const std::string name(fd->get_name());
            safety_error(fd, fd->line, "in safe code a method says how it uses its object: add a receiver as the first parameter of '" + name + "', as in '" + name + "(const self&)' to read it, '" + name + "(self&)' to change it, or '" + name + "(self)' to work on a copy; or make it 'static'");
        } else if (n->kind == K::OperatorFunctionDeclaration) {
            auto* op = static_cast<nodes::OperatorFunctionDeclaration*>(n);
            if (op->hoisted_friend) continue;
            if (op->get_parameters() && op->get_parameters()->has_receiver()) continue;
            const std::string name = op->symbol ? std::string(op->symbol->name) : std::string("this operator");
            safety_error(op, op->line, "in safe code a method says how it uses its object: add a receiver as the first parameter of '" + name + "', as in '" + name + "(const self&)' to read it, '" + name + "(self&)' to change it, or '" + name + "(self)' to work on a copy; or make it 'static'");
        }
    }
}

void TypeWalker::safety_error(const nodes::ASTNode* at, std::uint32_t line, const std::string& message) {
    const FileId file = file_of(at);
    if (m_policy && m_policy->quiet && m_policy->quiet(file)) return;
    const std::string key = std::to_string(file) + ":" + std::to_string(line) + ":" + message;
    if (!m_safety_seen.insert(key).second) return;
    SemanticError::emit(m_reporter, file, line, "Safety", message);
}

void TypeWalker::not_safe(nodes::ASTNode* at, const std::string& what) {
    safety_error(at, at->line, what + " isn't allowed in safe code; move it into a function that isn't marked 'safe' and call that");
}

void TypeWalker::check_safe_expression(nodes::ASTNode* e) {
    if (!nodes::in_safe_code(e)) return;
    auto raw_pointer = [this](nodes::ASTNode* n) {
        Type* t = type_of(n);
        if (t && t->is_reference()) t = static_cast<ReferenceType*>(t)->referent();
        return t && t->is_pointer();
    };
    auto is_this = [](const nodes::ASTNode* n) {
        return n && n->kind == K::Literal && static_cast<const nodes::Literal*>(n)->token_kind == tokenizing::Token::Kind::ThisKeyword;
    };

    auto followed = [](const nodes::ASTNode* n) {
        return n && n->kind == K::Identifier;
    };

    switch (e->kind) {
        case K::DereferenceExpression: {
            auto* d = static_cast<nodes::DereferenceExpression*>(e);
            if (!d->resolved && !is_this(d->operand) && !followed(d->operand) && raw_pointer(d->operand)) not_safe(e, "Reading through a raw pointer with '*' (other than a local pointer variable)");
            break;
        }
        case K::UnaryExpression: {
            auto* u = static_cast<nodes::UnaryExpression*>(e);
            if (u->resolved || !raw_pointer(u->operand)) break;
            if (u->op == tokenizing::Token::Kind::Asterisk) {
                if (!followed(u->operand)) not_safe(e, "Reading through a raw pointer with '*' (other than a local pointer variable)");
            } else if (u->op == tokenizing::Token::Kind::DoublePlus || u->op == tokenizing::Token::Kind::DoubleMinus) not_safe(e, "Moving a raw pointer with '++' or '--'");
            break;
        }
        case K::MemberAccessExpression: {
            auto* m = static_cast<nodes::MemberAccessExpression*>(e);
            if (m->is_arrow() && m->arrow_ops.empty() && !is_this(m->m_object) && !followed(m->m_object) && raw_pointer(m->m_object)) not_safe(e, "Using a raw pointer with '->' (other than a local pointer variable)");
            if (!m->is_scope() && m->resolved && m->resolved->owner && m->resolved->owner->node && m->resolved->owner->node->kind == K::RecordDeclaration
                && static_cast<nodes::RecordDeclaration*>(m->resolved->owner->node)->is_union()) not_safe(e, "Reading or writing a field of a union");
            break;
        }
        case K::SubscriptExpression: {
            auto* sub = static_cast<nodes::SubscriptExpression*>(e);
            if (!sub->resolved && raw_pointer(sub->m_array)) not_safe(e, "Indexing a raw pointer");
            break;
        }
        case K::BinaryExpression: {
            auto* b = static_cast<nodes::BinaryExpression*>(e);
            using TK = tokenizing::Token::Kind;
            if (b->resolved) break;
            const bool arith = b->op == TK::Plus || b->op == TK::Minus || b->op == TK::PlusEqual || b->op == TK::MinusEqual;
            if (arith && (raw_pointer(b->left) || raw_pointer(b->right))) not_safe(e, "Pointer arithmetic");
            break;
        }
        case K::CastExpression: {
            auto* c = static_cast<nodes::CastExpression*>(e);
            if (c->cast_kind == nodes::CastExpression::CastKind::Reinterpret) not_safe(e, "'reinterpret_cast'");
            else if (c->cast_kind == nodes::CastExpression::CastKind::Const) not_safe(e, "'const_cast'");
            break;
        }
        default: break;
    }
}

void TypeWalker::check_safe_statement(nodes::ASTNode* st) {
    if (!st || !nodes::in_safe_code(st)) return;
    if (st->kind == K::SingleStatement && static_cast<nodes::SingleStatement*>(st)->variant == nodes::SingleStatement::Variant::Goto) not_safe(st, "'goto'");
    if (st->kind == K::NativeCodeBlock) not_safe(st, "A C or C++ code block");
}

nodes::FunctionParameters* TypeWalker::declared_params(nodes::ASTNode* d) {
    if (!d) return nullptr;
    switch (d->kind) {
        case K::FunctionDeclaration:         return static_cast<nodes::FunctionDeclaration*>(d)->get_parameters();
        case K::OperatorFunctionDeclaration: return static_cast<nodes::OperatorFunctionDeclaration*>(d)->get_parameters();
        case K::ConstructorDeclaration:      return static_cast<nodes::ConstructorDeclaration*>(d)->get_parameters();
        case K::TemplateDeclaration:         return declared_params(static_cast<nodes::TemplateDeclaration*>(d)->m_declaration);
        default:                             return nullptr;
    }
}

bool TypeWalker::apply_keyword_arguments(nodes::CallExpression* c) {
    auto& args = c->m_arguments;
    std::size_t first_named = args.size();
    std::vector<std::pair<std::string_view, nodes::BinaryExpression*>> named;

    for (std::size_t i = 0; i < args.size(); ++i) {
        auto* b = args[i] && args[i]->kind == K::BinaryExpression ? static_cast<nodes::BinaryExpression*>(args[i]) : nullptr;
        if (b && b->named_argument) {
            if (first_named == args.size()) first_named = i;
            named.push_back({ static_cast<nodes::Identifier*>(b->left)->name, b });
        }
    }
    if (named.empty()) return true;

    auto as_assignment = [&]() {
        for (auto& [name, b] : named) {
            b->named_argument = false;
            auto* id = static_cast<nodes::Identifier*>(b->left);
            if (!id->resolved) SemanticError::unresolved_name(m_reporter, file_of(id), id->line, id->name, "");
        }
    };

    Symbol* head = generic_of_instance(callee_symbol(c->m_callee));
    if (head && head->kind == SymbolKind::Type && head->inner_scope) head = head->inner_scope->find_member("constructor");
    if (!head || head->kind != SymbolKind::Function) { as_assignment(); return true; }

    bool any_name = false;
    for (Symbol* o = head; o && !any_name; o = o->next_overload) {
        if (nodes::FunctionParameters* ps = declared_params(o->decl))
            for (auto& [name, b] : named) any_name = any_name || ps->find_by_name(name) != nullptr;
    }
    if (!any_name) { as_assignment(); return true; }

    for (std::size_t i = first_named; i < args.size(); ++i) {
        auto* b = args[i] && args[i]->kind == K::BinaryExpression ? static_cast<nodes::BinaryExpression*>(args[i]) : nullptr;
        if (!b || !b->named_argument) {
            SemanticError::emit(m_reporter, file_of(args[i]), args[i]->line, "Call", "an argument without a name can't come after a named one; put the plain arguments first");
            return false;
        }
    }

    std::string why;
    for (Symbol* o = head; o; o = o->next_overload) {
        nodes::FunctionParameters* ps = declared_params(o->decl);
        if (!ps) continue;
        const auto& params = ps->m_params;
        std::size_t last_pack = params.size();
        for (std::size_t j = 0; j < params.size(); ++j) if (params[j]->is_variadic()) last_pack = j;
        const std::size_t first_free = last_pack == params.size() ? first_named : last_pack + 1;
        if (last_pack == params.size() && first_named > params.size()) continue;

        std::vector<nodes::ASTNode*> filled(params.size(), nullptr);
        bool fits = true;
        for (auto& [name, b] : named) {
            std::size_t j = 0;
            while (j < params.size() && params[j]->get_name() != name) ++j;
            if (j == params.size()) {
                fits = false;
                why = "has no parameter named '" + std::string(name) + "'; its parameters are ";
                for (std::size_t k = 0; k < params.size(); ++k) why += (k ? ", " : "") + std::string(params[k]->get_name());
                break;
            }
            if (params[j]->is_variadic()) { fits = false; why = "can't take '" + std::string(name) + "' by name, because it takes several values; pass them without a name"; break; }
            if (j < first_free) { fits = false; why = "already gets '" + std::string(name) + "' from a plain argument"; break; }
            if (filled[j]) { fits = false; why = "is given '" + std::string(name) + "' twice"; break; }
            filled[j] = b->right;
        }
        if (!fits) continue;

        std::size_t last = first_free;
        for (std::size_t j = first_free; j < params.size(); ++j) if (filled[j]) last = j + 1;
        std::vector<nodes::ASTNode*> out(args.begin(), args.begin() + static_cast<std::ptrdiff_t>(first_named));
        for (std::size_t j = first_free; j < last && fits; ++j) {
            if (filled[j]) { out.push_back(filled[j]); continue; }
            if (!params[j]->has_initializer()) { fits = false; why = "needs a value for '" + std::string(params[j]->get_name()) + "', which has no default"; break; }
            Type* pt = nullptr;
            {
                EnvGuard g(m_types, env_of_symbol(o));
                pt = m_types.canonicalize(params[j]->get_type());
            }
            if (pt && pt->is_reference()) pt = static_cast<ReferenceType*>(pt)->referent();
            pt = pt ? m_types.strip_cv(pt) : nullptr;
            if (!pt || pt->is_dependent()) { fits = false; why = "can't skip '" + std::string(params[j]->get_name()) + "', because its type depends on a template parameter; give it a value"; break; }
            auto* gap = new nodes::Literal(std::string_view("default"), tokenizing::Token::Kind::DefaultKeyword, c->line);
            gap->file_id = c->file_id;
            gap->order = c->order;
            gap->safety = c->safety;
            m_default_args[gap] = pt;
            out.push_back(gap);
        }
        if (!fits) continue;
        args = std::move(out);
        return true;
    }

    const std::string name = head->name == "constructor" ? std::string("the constructor") : "'" + std::string(head->name) + "'";
    SemanticError::emit(m_reporter, file_of(c), c->line, "Call", name + " " + (why.empty() ? std::string("can't take these named arguments") : why));
    return false;
}

Type* TypeWalker::add_const(Type* t) {
    if (!t || t->is_reference() || t->is_error()) return t;
    CV c = t->cv();
    c.is_const = true;
    return m_types.with_cv(t, c);
}

bool TypeWalker::is_const_method(const Symbol* fn) {
    if (!fn || !fn->decl) return false;
    using FQ = modifiers::FunctionQualifiers;
    if (fn->decl->kind == K::FunctionDeclaration) {
        auto* f = static_cast<const nodes::FunctionDeclaration*>(fn->decl);
        return f->qualifiers().has(FQ::Const) || f->get_modifiers().has(modifiers::RawModifiers::Static);
    }
    if (fn->decl->kind == K::OperatorFunctionDeclaration) return static_cast<const nodes::OperatorFunctionDeclaration*>(fn->decl)->qualifiers().has(FQ::Const);
    return true;
}

bool TypeWalker::is_member_function(const Symbol* fn) const {
    const Scope* owner = fn ? fn->owner : nullptr;
    while (owner && owner->kind == Scope::Kind::Template) owner = owner->parent;
    return fn && owner && owner->kind == Scope::Kind::Record && fn->decl
        && (fn->decl->kind == K::FunctionDeclaration || fn->decl->kind == K::OperatorFunctionDeclaration);
}

void TypeWalker::check_const_call(Symbol* chosen, nodes::CallExpression* c) {
    if (!is_member_function(chosen) || is_const_method(chosen)) return;
    bool on_const = false;

    if (c->m_callee && c->m_callee->kind == K::MemberAccessExpression) {
        auto* m = static_cast<nodes::MemberAccessExpression*>(c->m_callee);
        if (m->is_scope()) on_const = m_in_const_method;
        else {
            Type* o = type_of(m->m_object);
            if (o && o->is_reference()) o = static_cast<ReferenceType*>(o)->referent();
            if (o && m->is_arrow() && o->is_pointer()) o = static_cast<PointerType*>(o)->pointee();
            on_const = o && o->cv().is_const;
        }
    } else {
        on_const = m_in_const_method;
    }

    if (on_const) SemanticError::emit(m_reporter, file_of(c), c->line, "Const", "cannot call non-const member function '" + std::string(chosen->name) + "' on a const object");
}

Type* TypeWalker::arrow_target(Type* obj, ArenaVector<Symbol*>* chain) {
    if (obj && obj->is_reference()) obj = static_cast<ReferenceType*>(obj)->referent();
    if (obj && obj->is_pointer()) return static_cast<PointerType*>(obj)->pointee();

    for (int guard = 0; obj && guard < 16; ++guard) {
        Symbol* op = nullptr;
        Type* ret = resolve_member_operator(obj, nodes::OverloadableOperator::Arrow, {}, &op);
        if (!ret) break;
        if (chain && op) chain->push_back(op);
        ensure_member_body(op);
        if (ret->is_reference()) ret = static_cast<ReferenceType*>(ret)->referent();
        if (ret->is_pointer()) return static_cast<PointerType*>(ret)->pointee();
        obj = ret;
    }

    return obj;
}

Type* TypeWalker::deduce_auto_declared(const parser_types::TypeInfo& ti, Type* init) {
    Type* base = deduce_auto(init);
    if (!base || ti.indirection.empty()) {
        if (base && ti.modifiers.has(modifiers::RawModifiers::Const)) { CV c = base->cv(); c.is_const = true; base = m_types.with_cv(base, c); }
        return base;
    }

    const parser_types::IndirectionQualifier& last = ti.indirection[ti.indirection.size() - 1];

    if (!last.is_pointer()) {
        Type* referent = init->is_reference() ? static_cast<ReferenceType*>(init)->referent() : init;
        CV c = referent->cv();
        if (ti.modifiers.has(modifiers::RawModifiers::Const)) c.is_const = true;
        referent = m_types.with_cv(m_types.strip_cv(referent), c);
        return m_types.reference(referent, last.is_rvalue_ref() ? RefQual::RValue : RefQual::LValue);
    }

    return base;
}

Type* TypeWalker::deduce_auto(Type* init) {
    if (!init) return nullptr;
    Type* t = init;
    if (t->is_reference()) t = static_cast<ReferenceType*>(t)->referent();
    if (m_types.strip_cv(t)->is_special_value()) return m_types.builtin(parser_types::PrimitiveType::BaseKind::Float);
    return m_types.strip_cv(t);
}

Type* TypeWalker::decay_ref(Type* t, VC& vc) {
    if (t && t->is_reference()) {
        vc = static_cast<ReferenceType*>(t)->ref_qual() == RefQual::RValue ? VC::RValue : VC::LValue;
        return static_cast<ReferenceType*>(t)->referent();
    }
    return t;
}

std::vector<ParamShape> TypeWalker::shapes_of(Symbol* fn) {
    std::vector<ParamShape> out;
    auto* decl = fn->decl;
    const nodes::FunctionParameters* ps = nullptr;

    if (decl && decl->kind == K::FunctionDeclaration) {
        ps = static_cast<nodes::FunctionDeclaration*>(decl)->get_parameters();
    } else if (decl && decl->kind == K::OperatorFunctionDeclaration) {
        ps = static_cast<nodes::OperatorFunctionDeclaration*>(decl)->get_parameters();
    } else if (decl && decl->kind == K::ConstructorDeclaration) {
        ps = static_cast<nodes::ConstructorDeclaration*>(decl)->get_parameters();
    }

    if (!ps) return out;
    out.reserve(ps->size());
    for (const nodes::FunctionParameter* p : ps->m_params) out.push_back(shape_of(p, m_types));
    return out;
}

Symbol* TypeWalker::callee_symbol(nodes::ASTNode* n) {
    if (!n) return nullptr;

    switch (n->kind) {
        case K::Identifier:             return static_cast<nodes::Identifier*>(n)->resolved;
        case K::QualifiedIdentifier:    return static_cast<nodes::QualifiedIdentifier*>(n)->resolved;
        case K::MemberAccessExpression: return static_cast<nodes::MemberAccessExpression*>(n)->resolved;
        default: return nullptr;
    }
}

std::string TypeWalker::plain_value_failure(Type* t, int depth) {
    using RM = modifiers::RawModifiers;
    using FQ = modifiers::FunctionQualifiers;
    using BK = parser_types::PrimitiveType::BaseKind;
    if (!t || t->is_error() || t->is_dependent() || depth > 32) return {};
    if (t->is_reference()) return "a reference";
    t = m_types.strip_cv(t);
    if (t->is_pointer() || t->is_enum() || t->is_null()) return {};

    if (t->is_array()) {
        auto* a = static_cast<ArrayType*>(t);
        if (!a->extent()) return "an array whose size is only known at run time";
        return plain_value_failure(a->element(), depth + 1);
    }

    if (t->is_builtin()) {
        switch (static_cast<BuiltinType*>(t)->base()) {
            case BK::Int: case BK::Float: case BK::Bool: case BK::Char: return {};
            default: return "'" + type_str(t) + "', which manages its own memory";
        }
    }

    if (!t->is_record()) return "'" + type_str(t) + "'";
    auto* rt = static_cast<RecordType*>(t);
    Symbol* owner = rt->decl();

    if (rt->is_instantiation() && m_types.record_scope_hook) {
        Scope* sc = m_types.record_scope_hook(rt);
        owner = sc ? sc->owner_symbol : nullptr;
    }

    if (!owner || !owner->decl || owner->decl->kind != K::RecordDeclaration) return {};
    auto* rd = static_cast<nodes::RecordDeclaration*>(owner->decl);
    EnvGuard g(m_types, env_of_symbol(owner));
    const std::string name = "'" + type_str(t) + "'";
    Type* self = record_type_of(owner);

    for (parser_types::TypeInfo* info : rd->all_bases()) {
        const std::string inner = plain_value_failure(m_types.canonicalize(*info), depth + 1);
        if (!inner.empty()) return name + ", because its base class is " + inner;
    }

    for (const nodes::RecordDeclaration::Member& m : rd->get_members()) {
        nodes::ASTNode* n = m.node;
        if (!n) continue;

        switch (n->kind) {
            case K::FunctionDeclaration:
                if (static_cast<nodes::FunctionDeclaration*>(n)->qualifiers().has_any(FQ::Virtual | FQ::Override)) return name + ", which has virtual functions";
                break;
            case K::DestructorDeclaration: {
                auto* d = static_cast<nodes::DestructorDeclaration*>(n);
                if (!d->is_defaulted()) return name + ", which has its own destructor";
                break;
            }
            case K::ConstructorDeclaration: {
                auto* c = static_cast<nodes::ConstructorDeclaration*>(n);
                if (!c->is_defaulted() && !c->is_deleted() && self_ref_kind(c->get_parameters(), self) != 0) return name + ", which has its own copy or move constructor";
                break;
            }
            case K::OperatorFunctionDeclaration: {
                auto* o = static_cast<nodes::OperatorFunctionDeclaration*>(n);
                if (o->qualifiers().has_any(FQ::Virtual | FQ::Override)) return name + ", which has virtual functions";
                if (o->overload == nodes::OverloadableOperator::Assign && !o->is_deleted()) return name + ", which has its own 'operator(=)'";
                break;
            }
            case K::VariableDeclaration: {
                auto* v = static_cast<nodes::VariableDeclaration*>(n);
                if (v->get_type_info().modifiers.has(RM::Static)) break;
                const std::string inner = plain_value_failure(m_types.canonicalize(v->get_type_info()), depth + 1);
                if (!inner.empty()) return name + ", because its member '" + std::string(v->get_name()) + "' is " + inner;
                break;
            }
            case K::ArrayDeclaration: {
                auto* a = static_cast<nodes::ArrayDeclaration*>(n);
                if (a->get_array_modifiers().has(RM::Static)) break;
                const std::string inner = plain_value_failure(array_symbol_type(a), depth + 1);
                if (!inner.empty()) return name + ", because its member '" + std::string(a->m_name) + "' is " + inner;
                break;
            }
            default: break;
        }
    }

    return {};
}

void TypeWalker::check_union(nodes::RecordDeclaration* r) {
    using RM = modifiers::RawModifiers;
    if (!r) return;

    if (!r->all_bases().empty()) SemanticError::emit(m_reporter, file_of(r), r->line, "Union", "union '" + std::string(r->get_name()) + "' cannot inherit from another type");
    const nodes::ASTNode* first_init = nullptr;

    for (const nodes::RecordDeclaration::Member& m : r->get_members()) {
        nodes::ASTNode* n = m.node;
        if (!n) continue;
        Type* ft = nullptr;
        std::string field;
        bool has_init = false;

        if (n->kind == K::VariableDeclaration) {
            auto* v = static_cast<nodes::VariableDeclaration*>(n);
            if (v->get_type_info().modifiers.has(RM::Static)) continue;
            ft = m_types.canonicalize(v->get_type_info());
            field = std::string(v->get_name());
            has_init = v->has_initializer();
        } else if (n->kind == K::ArrayDeclaration) {
            auto* a = static_cast<nodes::ArrayDeclaration*>(n);
            if (a->get_array_modifiers().has(RM::Static)) continue;
            ft = array_symbol_type(a);
            field = std::string(a->m_name);
            has_init = a->m_initializer != nullptr;
        } else {
            continue;
        }

        const std::string why = plain_value_failure(ft);
        if (!why.empty()) SemanticError::emit(m_reporter, file_of(n), n->line, "Union", "member '" + field + "' of union '" + std::string(r->get_name()) + "' is " + why + "; a union can only hold plain values: numbers, bool, char, pointers, enums, fixed-size arrays, and structs made only of those");

        if (has_init) {
            if (first_init) SemanticError::emit(m_reporter, file_of(n), n->line, "Union", "only one member of union '" + std::string(r->get_name()) + "' can have a default value, because only one member holds a value at a time");
            else first_init = n;
        }
    }
}

bool TypeWalker::text_displayable(Type* t) {
    using BK = parser_types::PrimitiveType::BaseKind;
    if (!t) return true;
    if (t->is_reference()) t = static_cast<ReferenceType*>(t)->referent();
    t = m_types.strip_cv(t);
    if (t->is_error() || t->is_dependent() || is_dynamic_t(t)) return true;
    if (t->is_pointer() || t->is_null() || t->is_enum() || t->is_special_value()) return true;

    if (t->is_builtin()) {
        const BK b = static_cast<BuiltinType*>(t)->base();
        return b != BK::Void && b != BK::Auto;
    }

    if (Type* held = special_held_type(t)) {
        Type* h = m_types.strip_cv(held);
        return h->is_builtin() && text_displayable(h);
    }

    if (t->is_record()) {
        ensure_conversions(t);
        return rank_user_defined(t, m_types.builtin(BK::String), m_types, false, nullptr) != ConversionRank::None
            || rank_user_defined(t, m_types.builtin(BK::Text), m_types, false, nullptr) != ConversionRank::None;
    }

    return false;
}

void TypeWalker::type_special_test(nodes::CastExpression* c) {
    static const char* const names[] = { "", "inf", "nan", "undefined", "indeterminate" };
    Type* src = type_of(c->operand);
    Type* sv = m_types.strip_cv(src && src->is_reference() ? static_cast<ReferenceType*>(src)->referent() : src);
    assign_typed(c, m_types.bool_(), VC::RValue);
    if (!sv || sv->is_error() || sv->is_dependent() || is_dynamic_t(sv) || sv->is_special_value() || special_held_type(sv)) return;
    using BK = parser_types::PrimitiveType::BaseKind;
    if (sv->is_builtin() && (static_cast<BuiltinType*>(sv)->base() == BK::Int || static_cast<BuiltinType*>(sv)->base() == BK::Float)) return;
    SemanticError::emit(m_reporter, file_of(c), c->line, "Type", "'is " + std::string(names[c->special_test < 5 ? c->special_test : 0]) + "' works on numbers, special(T) values and 'var' values, not on '" + type_str(sv) + "'; to let this value be " + names[c->special_test < 5 ? c->special_test : 0] + ", give it the type special(" + type_str(sv) + ")");
}

void TypeWalker::type_type_test(nodes::CastExpression* c) {
    if (c->special_test) { type_special_test(c); return; }
    const bool is_test = c->get_cast_kind() == nodes::CastExpression::CastKind::Is;
    const std::string op = is_test ? "is" : "as";
    Type* src = type_of(c->operand);
    Type* target = m_types.canonicalize(c->get_target());
    Type* sv = m_types.strip_cv(src && src->is_reference() ? static_cast<ReferenceType*>(src)->referent() : src);
    Type* tv = target ? m_types.strip_cv(target) : nullptr;
    auto finish = [&](bool lvalue) {
        Type* result = is_test ? m_types.bool_() : tv;
        if (!is_test && lvalue && src && type_is_const(src) && tv) { CV cv = tv->cv(); cv.is_const = true; result = m_types.with_cv(tv, cv); }
        assign_typed(c, result, lvalue ? VC::LValue : VC::RValue);
    };

    if (!sv || !tv || sv->is_error() || tv->is_error() || sv->is_dependent() || tv->is_dependent()) { finish(false); return; }

    if (target->is_reference()) {
        SemanticError::emit(m_reporter, file_of(c), c->line, "Type", "'" + op + "' needs a type without '&', such as '" + type_str(tv) + "'");
        finish(false);
        return;
    }

    if (sv->kind() == TypeKind::Variant) {
        bool found = false;
        for (Type* alt : static_cast<VariantType*>(sv)->alternatives()) if (m_types.strip_cv(alt) == tv) found = true;
        if (!found) SemanticError::emit(m_reporter, file_of(c), c->line, "Type", "'" + type_str(sv) + "' can never hold a '" + type_str(tv) + "'");
        finish(!is_test && is_lvalue(c->operand));
        return;
    }

    if (is_dynamic_t(sv)) {
        const bool object = dynamic_object_type(tv, m_types);
        if (is_dynamic_t(tv) || !dynamic_value_type(tv, m_types, false)) SemanticError::emit(m_reporter, file_of(c), c->line, "Type", "a 'var' can't hold a '" + type_str(tv) + "', so '" + op + " " + type_str(tv) + "' can never match");
        finish(!is_test && object && is_lvalue(c->operand));
        return;
    }

    Type* from = sv;
    Type* to = tv;
    const bool pointers = sv->is_pointer() && tv->is_pointer();
    if (pointers) {
        from = m_types.strip_cv(static_cast<PointerType*>(sv)->pointee());
        to = m_types.strip_cv(static_cast<PointerType*>(tv)->pointee());
    }

    if (record_symbol_of(from) && record_symbol_of(to)) {
        const bool related = from == to || rank_conversion(m_types.pointer(to), m_types.pointer(from), m_types) != ConversionRank::None;
        if (!related) SemanticError::emit(m_reporter, file_of(c), c->line, "Type", "'" + type_str(to) + "' does not inherit from '" + type_str(from) + "', so '" + op + " " + type_str(tv) + "' can never match");
        else if (from != to && !is_polymorphic(from)) SemanticError::emit(m_reporter, file_of(c), c->line, "Type", "'" + op + "' needs '" + type_str(from) + "' to have a virtual function, so the object's real type can be checked while the program runs");
        finish(!is_test && !pointers && is_lvalue(c->operand));
        return;
    }

    SemanticError::emit(m_reporter, file_of(c), c->line, "Type", "'" + op + "' works on variants, 'var' values, and objects or pointers of classes with virtual functions, not on '" + type_str(sv) + "'");
    finish(false);
}

void TypeWalker::report_not_dynamic(Type* from, nodes::ASTNode* site) {
    Type* f = m_types.strip_cv(from && from->is_reference() ? static_cast<ReferenceType*>(from)->referent() : from);
    std::string why = "a 'var' can hold numbers, bool, char, string, text, objects, enums, pointers, fixed-size arrays, variants, and functions whose parameters and result are those types";
    if (f && f->is_function()) why += "; this function's parameters or result can't be held in a 'var' (a non-const reference parameter can't either)";
    SemanticError::emit(m_reporter, file_of(site), site->line, "Type", "cannot store a value of type '" + type_str(f) + "' in a 'var': " + why);
}

void TypeWalker::check_convertible(Type* from, Type* to, nodes::ASTNode* site) {
    if (!from || !to) return;
    if (from->is_error() || to->is_error()) return;
    if (is_dynamic_t(from) || is_dynamic_t(to)) return;   
    if (rank_conversion(from, to, m_types) != ConversionRank::None) return;

    Type* f = m_types.strip_cv(from->is_reference() ? static_cast<ReferenceType*>(from)->referent() : from);
    Type* t = m_types.strip_cv(to->is_reference() ? static_cast<ReferenceType*>(to)->referent() : to);
    using BK = parser_types::PrimitiveType::BaseKind;
    auto numeric = [](Type* x) { return x && x->is_builtin() && (static_cast<BuiltinType*>(x)->base() == BK::Int || static_cast<BuiltinType*>(x)->base() == BK::Float); };

    if (f && f->is_special_value() && t && t->is_builtin() && static_cast<BuiltinType*>(t)->is_hardware() && static_cast<BuiltinType*>(t)->base() == BK::Int) {
        SemanticError::emit(m_reporter, file_of(site), site->line, "Type",
            "'" + type_str(t) + "' is C++'s own integer type, which can't hold inf, nan, undefined or indeterminate; use a Walnut integer type such as 'int' to keep special values");
        return;
    }

    if (f && f->is_special_value() && t) {
        SemanticError::emit(m_reporter, file_of(site), site->line, "Type",
            "a special value (inf, nan, undefined or indeterminate) can only go into a number type or a special(T); declare this as 'special(" + type_str(t) + ")' to allow it");
        return;
    }

    if (numeric(f) && numeric(t) && !static_cast<BuiltinType*>(f)->is_unsigned() && static_cast<BuiltinType*>(t)->is_unsigned()) {
        SemanticError::emit(m_reporter, file_of(site), site->line, "Type",
            "no implicit conversion from '" + type_str(from) + "' to '" + type_str(to) + "': a negative value would be lost, so convert it explicitly with static_cast<" + type_str(t) + ">(...)");
        return;
    }

    SemanticError::not_convertible(m_reporter, file_of(site), site->line, type_str(from), type_str(to));
}

} // namespace semantics
} // namespace walnut
