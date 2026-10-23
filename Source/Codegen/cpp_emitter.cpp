#include "Codegen/cpp_emitter.hpp"
#include "Parser/Nodes/node_children.hpp"
#include "Lexer/numeric_literal.hpp"
#include "Common/file_kind.hpp"
#include "Common/source_manager.hpp"

#include <cstring>

namespace walnut {
namespace codegen {

CppEmitter::CppEmitter(semantics::TypeContext& types, semantics::Instantiator& inst, ErrorReporter& reporter) : m_types(types), m_inst(inst), m_reporter(reporter), m_eval(types)
{
    m_eval.env_of = [this](Sym* s) -> const semantics::SubstEnv* {
        Inst* I = m_inst.owning(s);
        return I ? &I->env : nullptr;
    };
    m_eval.variable_template_value = [this](nodes::TemplateInstantiation* t) -> semantics::ConstValue {
        auto& hook = m_inst.evaluator().variable_template_value;
        return hook ? hook(t) : semantics::ConstValue{};
    };
}

bool CppEmitter::emit(const std::vector<CodegenUnit>& units, std::ostream& out) {
    collect(units);
    if (m_failed) return false;
    if (!m_main_fn && m_script.empty()) {
        fail_at(nullptr, "program has no entry point: define 'function main() -> int' or write top-level statements");
        return false;
    }

    std::ostringstream records, protos, globals, defs, init, native_decls, native_wrappers, equality;
    m_track_globals = true;
    emit_records(records);
    emit_prototypes(protos);
    emit_native_externs(native_decls, native_wrappers);
    emit_definitions(defs);
    finish_global_usage();
    emit_globals(globals);
    emit_init(init);
    emit_equality_helpers(equality);
    if (m_failed) return false;

    out << "#include \"walnut_runtime.hpp\"\n\n";
    out << native_decls.str();
    out << "namespace wlu {\n\n";
    for (const RecordInfo* r : m_record_order) out << (r->decl->is_union() ? "union " : "struct ") << r->cname << ";\n";
    if (!m_record_order.empty()) out << "\n";
    out << m_enums.str();
    out << m_consts.str() << (m_consts.str().empty() ? "" : "\n");
    out << records.str();
    out << protos.str() << "\n";
    out << equality.str();
    out << native_wrappers.str();
    out << globals.str() << "\n";
    out << defs.str();
    out << init.str();
    out << "} // namespace wlu\n\n";
    out << main_function_code();
    return !m_failed;
}

std::string CppEmitter::main_function_code() {
    std::ostringstream out;
    out << "#if defined(WALNUT_GC)\n#include \"walnut_gc_main.hpp\"\n#endif\n\n";
    out << "int main(int argc, char** argv) {\n";
    out << "    return walnut_rt::run_program([&]() -> int {\n";
    if (unbuffered) out << "        walnut_rt::io_native::unbuffered_request() = true;\n";
    out << "        wlu::walnut_init();\n";

    if (m_main_fn) {
        Type* rt = return_type_of(m_main_fn);
        const std::size_t nparams = param_types_of(m_main_fn).size();
        std::string args;
        if (nparams >= 1) { out << "        auto walnut_args = walnut_rt::program_args(argc, argv);\n"; args = "walnut_args"; }
        if (rt && !is_void(rt)) out << "        return walnut_rt::exit_code(wlu::" << function_name(m_main_fn) << "(" << args << "));\n";
        else                    out << "        wlu::" << function_name(m_main_fn) << "(" << args << ");\n        return 0;\n";
    } else {
        out << "        return 0;\n";
    }

    out << "    });\n}\n";
    return out.str();
}

bool CppEmitter::needs_header_body(const FuncInfo& f) {
    const modifiers::RawModifiers* mods = nullptr;
    if (f.decl->kind == K::FunctionDeclaration) mods = &static_cast<nodes::FunctionDeclaration*>(f.decl)->get_modifiers();
    else if (f.decl->kind == K::OperatorFunctionDeclaration) mods = &static_cast<nodes::OperatorFunctionDeclaration*>(f.decl)->get_modifiers();
    if (mods && mods->has(RM::Inline)) return true;
    if (!callable_params(f).empty()) return true;
    return function_header(f, false, false).find("auto") != std::string::npos;
}

void CppEmitter::scan_function_values(const std::vector<CodegenUnit>& units) {
    std::unordered_set<const nodes::ASTNode*> callees;

    std::function<void(nodes::ASTNode*)> visit = [&](nodes::ASTNode* n) {
        if (!n) return;

        if (n->kind == K::CallExpression) {
            if (nodes::ASTNode* callee = static_cast<nodes::CallExpression*>(n)->m_callee) {
                callees.insert(callee);
                if (callee->kind == K::TemplateInstantiation) callees.insert(static_cast<nodes::TemplateInstantiation*>(callee)->m_template);
            }
        }

        const Sym* target = nullptr;
        std::string_view name;

        if (n->kind == K::Identifier) {
            target = static_cast<nodes::Identifier*>(n)->resolved;
            name = static_cast<nodes::Identifier*>(n)->name;
        } else if (n->kind == K::QualifiedIdentifier) {
            auto* q = static_cast<nodes::QualifiedIdentifier*>(n);
            target = q->resolved;
            if (!q->parts().empty()) name = q->parts().back();
        } else if (n->kind == K::MemberAccessExpression) {
            target = static_cast<nodes::MemberAccessExpression*>(n)->resolved;
            name = static_cast<nodes::MemberAccessExpression*>(n)->m_member;
        }

        if (target && target->kind == SK::Function && !callees.count(n) && !name.empty()) m_value_used_functions.insert(name);
        nodes::for_each_child<true>(n, visit);
    };

    for (const CodegenUnit& u : units) visit(u.ast);
    m_inst.for_each([&](Inst& I) { visit(I.decl); });
}

bool CppEmitter::only_called(const nodes::ASTNode* body, std::string_view name) {
    std::unordered_set<const nodes::ASTNode*> allowed;
    bool ok = true;

    std::function<void(nodes::ASTNode*)> visit = [&](nodes::ASTNode* n) {
        if (!n || !ok) return;

        if (n->kind == K::CallExpression) {
            auto* c = static_cast<nodes::CallExpression*>(n);
            if (c->m_callee) allowed.insert(c->m_callee);
            std::vector<Type*> slots = c->resolved ? param_types_of(c->resolved) : std::vector<Type*>{};

            for (std::size_t i = 0; i < c->m_arguments.size(); ++i) {
                nodes::ASTNode* a = c->m_arguments[i];
                if (!a || a->kind != K::Identifier || static_cast<nodes::Identifier*>(a)->name != name) continue;
                Type* slot = i < slots.size() ? slots[i] : nullptr;
                if (!c->resolved || !slot) { ok = false; return; }
                if (slot->is_reference() && !static_cast<semantics::ReferenceType*>(slot)->referent()->cv().is_const) { ok = false; return; }
                allowed.insert(a);
            }
        }

        if (n->kind == K::Identifier && static_cast<nodes::Identifier*>(n)->name == name && !allowed.count(n)) { ok = false; return; }
        nodes::for_each_child<true>(n, visit);
    };

    visit(const_cast<nodes::ASTNode*>(body));
    return ok;
}

auto CppEmitter::callable_params(const FuncInfo& f) -> const std::vector<const nodes::FunctionParameter*>& {
    auto found = m_callable_params.find(f.decl);
    if (found != m_callable_params.end()) return found->second;
    std::vector<const nodes::FunctionParameter*>& out = m_callable_params[f.decl];
    if (f.decl->kind != K::FunctionDeclaration || f.sym == m_main_fn || decl_is_async(f.decl)) return out;
    auto* fd = static_cast<nodes::FunctionDeclaration*>(f.decl);
    const modifiers::FunctionQualifiers& q = fd->qualifiers();
    if (!fd->get_body() || q.has(FQ::Virtual) || q.has(FQ::Override) || q.has(FQ::Abstract)) return out;
    if (fd->get_modifiers().has(RM::Extern) || (f.owner && f.owner->native) || !fd->native_name.empty()) return out;
    if (m_value_used_functions.count(fd->get_name())) return out;
    const nodes::FunctionParameters* ps = fd->get_parameters();
    if (!ps) return out;
    EnvScope outer(m_types, outer_env(f));
    EnvScope env(m_types, f.inst ? &f.inst->env : nullptr);
    Type* ret = value_type(return_type_of(f.sym));
    if (ret && ret->kind() == semantics::TypeKind::Coroutine) return out;

    for (const nodes::FunctionParameter* p : ps->m_params) {
        if (!p || p->expanded_pack || p->is_variadic() || p->has_initializer() || p->get_name().empty()) continue;
        Type* pt = m_types.canonicalize(const_cast<nodes::FunctionParameter*>(p)->get_type());
        Type* vt = value_type(pt);
        if (!vt || vt->kind() != semantics::TypeKind::Function) continue;
        if (pt->is_reference() && !static_cast<semantics::ReferenceType*>(pt)->referent()->cv().is_const) continue;
        if (only_called(fd->get_body(), p->get_name())) out.push_back(p);
    }

    return out;
}

bool CppEmitter::emit_split(const std::vector<CodegenUnit>& units, SplitProgram& out) {
    collect(units);
    if (m_failed) return false;
    if (!m_main_fn && m_script.empty()) {
        fail_at(nullptr, "program has no entry point: define 'function main() -> int' or write top-level statements");
        return false;
    }

    std::ostringstream records, protos, globals_extern, globals, header_defs, init, native_decls, native_wrappers, equality;
    std::vector<std::pair<std::uint32_t, std::ostringstream>> groups;

    auto group_for = [&](std::uint32_t file) -> std::ostringstream& {
        for (auto& g : groups) if (g.first == file) return g.second;
        groups.emplace_back(file, std::ostringstream());
        return groups.back().second;
    };

    m_track_globals = true;
    emit_records(records);
    emit_prototypes(protos);
    emit_native_externs(native_decls, native_wrappers);
    for (RecordInfo* r : m_record_order) emit_record_definitions(group_for(r->decl->file_id), r);

    for (const FuncInfo& f : m_functions) {
        if (needs_header_body(f)) emit_function_body(header_defs, f);
        else emit_function_body(group_for(f.decl->file_id), f);
    }

    finish_global_usage();
    emit_globals(globals_extern, true);
    emit_globals(globals);

    emit_init(init);
    emit_equality_helpers(equality);
    if (m_failed) return false;

    std::ostringstream h;
    h << "#ifndef WALNUT_PROGRAM_HPP\n#define WALNUT_PROGRAM_HPP\n\n";
    h << native_decls.str();
    h << "namespace wlu {\n\n";
    for (const RecordInfo* r : m_record_order) h << (r->decl->is_union() ? "union " : "struct ") << r->cname << ";\n";
    if (!m_record_order.empty()) h << "\n";
    h << m_enums.str();
    h << m_consts.str() << (m_consts.str().empty() ? "" : "\n");
    h << records.str();
    h << protos.str() << "\n";
    h << equality.str();
    h << native_wrappers.str();
    h << globals_extern.str() << "\n";
    h << "void walnut_init();\n\n";
    h << header_defs.str();
    h << "} // namespace wlu\n\n#endif // WALNUT_PROGRAM_HPP\n";
    out.header = h.str();

    const std::string prelude = "#include \"walnut_runtime.hpp\"\n#include \"program.hpp\"\n\nnamespace wlu {\n\n";
    std::size_t index = 0;

    for (auto& g : groups) {
        std::string stem = "unit";

        if (source_path && g.first != static_cast<std::uint32_t>(-1)) {
            stem = std::filesystem::path(source_path(FileId(g.first))).stem().string();
            for (char& ch : stem) if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') ch = '_';
            if (stem.empty()) stem = "unit";
        }

        out.sources.emplace_back(std::to_string(index++) + "_" + stem + ".cpp", prelude + g.second.str() + "} // namespace wlu\n");
    }

    std::string definitions;
    {
        std::istringstream lines(globals.str());
        std::string line;
        while (std::getline(lines, line)) if (line.rfind("inline ", 0) != 0) definitions += line + "\n";
    }

    out.sources.emplace_back("main.cpp", prelude + definitions + "\n" + init.str() + "} // namespace wlu\n\n" + main_function_code());
    return !m_failed;
}

void CppEmitter::fail_at(const nodes::ASTNode* at, const std::string& what) {
    m_failed = true;
    std::ostringstream ss;
    const std::size_t line = at ? at->line : 0;
    ss << "Codegen Error on line " << line << ": " << what;
    m_reporter.report(ErrorPhase::Semantic, at ? FileId(at->file_id) : INVALID_FILE, line, ss.str());
}

void CppEmitter::collect_record(nodes::RecordDeclaration* rd, Inst* inst, Inst* env) {
    Sym* sym = inst ? inst->sym : rd->symbol;
    if (!sym || rd->m_is_declaration) return;
    if (m_record_by_sym.count(sym)) return;
    auto info = std::make_unique<RecordInfo>();
    info->decl = rd;
    info->sym  = sym;
    info->inst = inst;
    info->env  = inst ? inst : env;
    info->cname = unique_global(std::string("R_") + sanitize(qualified_name(sym)) + (inst ? "_" + mangle_args(inst->args) : ""));
    info->native = rd->m_native;
    RecordInfo* raw = info.get();
    m_records.push_back(std::move(info));
    m_record_by_sym[sym] = raw;

    for (const auto& member : rd->get_members()) {
        nodes::ASTNode* n = member.node;
        if (!n) continue;

        switch (n->kind) {
            case K::FunctionDeclaration: {
                auto* f = static_cast<nodes::FunctionDeclaration*>(n);
                if (f->get_modifiers().has(RM::Friend) || f->lazy_untyped) break;
                if (f->has_body() && f->symbol) m_functions.push_back(FuncInfo{ n, f->symbol, raw->env, raw });
                break;
            }
            case K::OperatorFunctionDeclaration: {
                auto* f = static_cast<nodes::OperatorFunctionDeclaration*>(n);
                if (f->get_modifiers().has(RM::Friend) || f->lazy_untyped) break;
                if (f->has_body() && f->symbol) m_functions.push_back(FuncInfo{ n, f->symbol, raw->env, raw });
                break;
            }
            case K::RecordDeclaration:
                collect_record(static_cast<nodes::RecordDeclaration*>(n), nullptr, raw->env);
                break;
            case K::EnumDeclaration:
                m_enum_decls.push_back(static_cast<nodes::EnumDeclaration*>(n));
                break;
            default:
                break;
        }
    }
}

void CppEmitter::collect_decl(nodes::ASTNode* n, bool top_level) {
    if (!n) return;

    switch (n->kind) {
        case K::FunctionDeclaration: {
            auto* f = static_cast<nodes::FunctionDeclaration*>(n);
            if (f->has_body() && f->symbol) {
                m_functions.push_back(FuncInfo{ n, f->symbol, nullptr, nullptr });
                if (f->get_name() == "main" && top_level && !m_main_fn) m_main_fn = f->symbol;
            } else if (f->symbol && f->get_modifiers().is_native_extern()) {
                m_native_externs.push_back(f->symbol);
            }
            break;
        }

        case K::OperatorFunctionDeclaration: {
            auto* f = static_cast<nodes::OperatorFunctionDeclaration*>(n);
            if (f->has_body() && f->symbol) m_functions.push_back(FuncInfo{ n, f->symbol, nullptr, nullptr });
            break;
        }

        case K::RecordDeclaration:
            collect_record(static_cast<nodes::RecordDeclaration*>(n), nullptr);
            break;

        case K::EnumDeclaration:
            m_enum_decls.push_back(static_cast<nodes::EnumDeclaration*>(n));
            break;

        case K::NamespaceDeclaration: {
            auto* ns = static_cast<nodes::NamespaceDeclaration*>(n);
            if (ns->m_body) for (nodes::ASTNode* s : ns->m_body->statements) collect_decl(s, false);
            break;
        }

        case K::NativeCodeBlock: {
            auto* b = static_cast<nodes::NativeCodeBlock*>(n);
            if (std::find(m_native_code_seen.begin(), m_native_code_seen.end(), b->code.data()) != m_native_code_seen.end()) break;
            m_native_code_seen.push_back(b->code.data());
            std::string path = path_of_text ? path_of_text(b->code.data()) : std::string();
            if (path.empty() && source_path) path = source_path(FileId(b->file_id));
            m_native_code.push_back(NativeCode{ std::string(b->code), b->is_c, path, b->line });
            break;
        }

        case K::VariableDeclaration:
        case K::ArrayDeclaration:
            if (n->kind == K::VariableDeclaration && static_cast<nodes::VariableDeclaration*>(n)->get_type_info().modifiers.has(RM::Extern)) break;
            m_global_decls.push_back(n);
            m_script.push_back(n);
            if (prunable_global(n)) m_prunable.insert(static_cast<nodes::VariableDeclaration*>(n)->symbol);
            break;

        case K::TemplateDeclaration:
        case K::ConceptDeclaration:
        case K::StaticAssertDeclaration:
        case K::UsingDeclaration:
        case K::ImportExportDeclaration:
        case K::ModuleDeclaration:
        case K::TypeQueryOverload:
            break;

        default:
            if (top_level) m_script.push_back(n);
            else unsupported(n, "a statement inside a namespace");
            break;
    }
}

bool CppEmitter::signature_failed(semantics::Type* t) {
    if (!t) return false;
    if (t->is_error()) return true;
    if (t->kind() != semantics::TypeKind::Function) return false;
    auto* f = static_cast<semantics::FunctionType*>(t);
    if (f->ret() && f->ret()->is_error()) return true;
    for (semantics::Type* p : f->params()) if (p && p->is_error()) return true;
    return false;
}

void CppEmitter::collect(const std::vector<CodegenUnit>& units) {
    for (const CodegenUnit& u : units) {
        if (!u.ast) continue;
        for (nodes::ASTNode* s : u.ast->statements) collect_decl(s, true);
    }

    scan_function_values(units);

    std::vector<Inst*> instances;
    m_inst.for_each([&](Inst& I) { instances.push_back(&I); });

    for (Inst* I : instances) {
        if (!I->decl || !I->typed || contains_dependent_args(I->args)) continue;
        if (I->decl->kind == K::RecordDeclaration) collect_record(static_cast<nodes::RecordDeclaration*>(I->decl), I);
    }

    for (Inst* I : instances) {
        if (!I->decl || !I->typed) continue;
        if (contains_dependent_args(I->args)) continue;

        if (I->decl->kind == K::RecordDeclaration) {
            continue;
        } else if (I->decl->kind == K::FunctionDeclaration) {
            auto* f = static_cast<nodes::FunctionDeclaration*>(I->decl);
            if (signature_failed(I->type)) continue;
            if (f->has_body() && I->sym) m_functions.push_back(FuncInfo{ I->decl, I->sym, I, owner_record_of(I->sym) });
        } else if (I->decl->kind == K::ConstructorDeclaration) {
            if (signature_failed(I->type) || !I->sym || !owner_record_of(I->sym)) continue;
            if (static_cast<nodes::ConstructorDeclaration*>(I->decl)->m_special != nodes::ConstructorDeclaration::Special::None) continue;
            if (ctor_instance_self_kind(I) == 1) continue;
            m_ctor_insts.push_back(I);
        }
    }

    for (nodes::ASTNode* s : m_script) collect_locals(s, nullptr);

    for (std::size_t i = 0; i < m_functions.size(); ++i) {
        const FuncInfo f = m_functions[i];
        collect_locals(f.decl->kind == K::FunctionDeclaration ? static_cast<nodes::FunctionDeclaration*>(f.decl)->get_body() : static_cast<nodes::OperatorFunctionDeclaration*>(f.decl)->m_body, f.inst);
    }

    for (std::size_t i = 0; i < m_records.size(); ++i) {
        RecordInfo* r = m_records[i].get();
        for (const auto& member : r->decl->get_members()) {
            if (!member.node) continue;
            if (member.node->kind == K::ConstructorDeclaration) collect_locals(static_cast<nodes::ConstructorDeclaration*>(member.node)->m_body, r->env);
            if (member.node->kind == K::DestructorDeclaration) collect_locals(static_cast<nodes::DestructorDeclaration*>(member.node)->m_body, r->env);
        }
    }

    for (Inst* I : m_ctor_insts) collect_locals(static_cast<nodes::ConstructorDeclaration*>(I->decl)->m_body, I);

    order_records();
}

void CppEmitter::collect_locals(nodes::ASTNode* n, Inst* env) {
    if (!n) return;
    if (n->kind == K::RecordDeclaration) { collect_record(static_cast<nodes::RecordDeclaration*>(n), nullptr, env); return; }
    if (n->kind == K::EnumDeclaration) { m_enum_decls.push_back(static_cast<nodes::EnumDeclaration*>(n)); return; }
    if (n->kind == K::TemplateDeclaration) return;
    nodes::for_each_child(n, [&](nodes::ASTNode* c) { collect_locals(c, env); });
}

bool CppEmitter::contains_dependent_args(const std::vector<semantics::TemplateArg>& args) {
    for (const auto& a : args) {
        if (a.is_dependent_value()) return true;
        if (a.is_type && a.type && a.type->is_dependent()) return true;
    }
    return false;
}

auto CppEmitter::owner_record_of(Sym* s) -> RecordInfo* {
    for (semantics::Scope* sc = s ? s->owner : nullptr; sc; sc = sc->parent) {
        if (sc->kind == semantics::Scope::Kind::Record && sc->owner_symbol) {
            auto it = m_record_by_sym.find(sc->owner_symbol);
            return it == m_record_by_sym.end() ? nullptr : it->second;
        }
        if (sc->kind == semantics::Scope::Kind::Function) return nullptr;
        if (sc->kind == semantics::Scope::Kind::Template && sc->node && sc->node->kind == K::TemplateDeclaration) return nullptr;
    }
    return nullptr;
}

void CppEmitter::order_records() {
    std::unordered_map<RecordInfo*, int> state;
    std::function<void(RecordInfo*)> visit = [&](RecordInfo* r) {
        int& st = state[r];
        if (st == 2) return;
        if (st == 1) { fail_at(r->decl, "records '" + r->cname + "' contain each other by value"); return; }
        st = 1;
        EnvScope env(m_types, r->env ? &r->env->env : nullptr);

        for (parser_types::TypeInfo* info : r->decl->all_bases()) {
            if (RecordInfo* b = record_info_of(m_types.canonicalize(*info))) visit(b);
        }

        if (r->native && r->inst) {
            for (const semantics::TemplateArg& a : r->inst->args) {
                if (a.is_type && a.type) for (RecordInfo* dep : value_dependencies(a.type)) visit(dep);
            }
        }

        for (const auto& member : r->decl->get_members()) {
            if (!member.node || member.node->kind != K::VariableDeclaration) continue;
            auto* v = static_cast<nodes::VariableDeclaration*>(member.node);
            if (v->get_type_info().modifiers.has(RM::Static)) continue;
            for (RecordInfo* dep : value_dependencies(m_types.canonicalize(v->get_type_info()))) visit(dep);
        }

        if (r->native) {
            auto soft = [&](Type* t) {
                t = m_types.strip_cv(t);
                if (t && t->is_reference()) t = m_types.strip_cv(static_cast<semantics::ReferenceType*>(t)->referent());
                for (RecordInfo* dep : value_dependencies(t)) {
                    if (dep != r && state[dep] == 0) visit(dep);
                }
            };
            for (const auto& member : r->decl->get_members()) {
                nodes::ASTNode* n = member.node;
                if (!n || (n->kind != K::FunctionDeclaration && n->kind != K::OperatorFunctionDeclaration)) continue;
                Sym* fs = n->kind == K::FunctionDeclaration ? static_cast<nodes::FunctionDeclaration*>(n)->symbol : static_cast<nodes::OperatorFunctionDeclaration*>(n)->symbol;
                const bool lazy = n->kind == K::FunctionDeclaration ? static_cast<nodes::FunctionDeclaration*>(n)->lazy_untyped : static_cast<nodes::OperatorFunctionDeclaration*>(n)->lazy_untyped;
                const bool used = n->kind == K::FunctionDeclaration ? static_cast<nodes::FunctionDeclaration*>(n)->native_used : static_cast<nodes::OperatorFunctionDeclaration*>(n)->native_used;
                if (!fs || lazy || !used) continue;
                soft(return_type_of(fs));
                if (const nodes::FunctionParameters* ps = params_of(n)) {
                    for (nodes::FunctionParameter* p : ps->m_params) {
                        if (p) soft(m_types.canonicalize(p->get_type()));
                    }
                }
            }
        }

        st = 2;
        m_record_order.push_back(r);
    };

    for (auto& r : m_records) visit(r.get());
}

auto CppEmitter::value_dependencies(Type* t) -> std::vector<RecordInfo*> {
    std::vector<RecordInfo*> out;
    t = m_types.strip_cv(t);
    if (!t) return out;
    if (t->is_record()) { if (RecordInfo* r = record_info_of(t)) out.push_back(r); }
    else if (t->is_array()) { auto inner = value_dependencies(static_cast<semantics::ArrayType*>(t)->element()); out.insert(out.end(), inner.begin(), inner.end()); }
    return out;
}

auto CppEmitter::record_info_of(Type* t) -> RecordInfo* {
    t = m_types.strip_cv(t);
    if (!t || !t->is_record()) return nullptr;
    auto* rt = static_cast<semantics::RecordType*>(t);

    if (rt->is_instantiation()) {
        Inst* I = m_inst.for_record(rt, nullptr);
        if (!I || !I->sym) return nullptr;
        auto it = m_record_by_sym.find(I->sym);
        return it == m_record_by_sym.end() ? nullptr : it->second;
    }

    auto it = m_record_by_sym.find(rt->decl());
    return it == m_record_by_sym.end() ? nullptr : it->second;
}

std::string CppEmitter::sanitize(std::string_view s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());

    for (char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (ok) out += static_cast<char>(c);
        else if (c == '_') out += "_u";
        else if (c == ' ') continue;
        else { out += '_'; out += hex[c >> 4]; out += hex[c & 15]; }
    }

    return out.empty() ? std::string("x") : out;
}

std::string CppEmitter::unique_global(std::string base) {
    std::string name = base;
    int n = 2;
    while (m_used_global_names.count(name)) name = base + "_" + std::to_string(n++);
    m_used_global_names.insert(name);
    return name;
}

std::string CppEmitter::qualified_name(const Sym* s) {
    std::vector<std::string_view> parts;
    parts.push_back(s->name);

    for (const semantics::Scope* sc = s->owner; sc; sc = sc->parent) {
        if (sc->owner_symbol && (sc->kind == semantics::Scope::Kind::Namespace || sc->kind == semantics::Scope::Kind::Record)) parts.push_back(sc->owner_symbol->name);
    }

    std::string out;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) { if (!out.empty()) out += "::"; out += std::string(*it); }
    return out;
}

std::string CppEmitter::mangle_args(const std::vector<semantics::TemplateArg>& args) {
    std::string out;
    for (const auto& a : args) {
        if (!out.empty()) out += "_";
        if (a.is_type) out += type_mangle(a.type);
        else if (a.value) out += "v" + sanitize(a.value->to_string());
        else if (a.fvalue) { std::ostringstream os; os << *a.fvalue; out += "f" + sanitize(os.str()); }
        else if (a.svalue) {
            static const char* hex = "0123456789abcdef";
            out += "s";
            for (unsigned char c : *a.svalue) { out += hex[c >> 4]; out += hex[c & 15]; }
        }
    }
    return out;
}

bool CppEmitter::is_global_scope_owner(const Sym* s) const {
    const auto k = owner_kind(s);
    return k == semantics::Scope::Kind::Module || k == semantics::Scope::Kind::Namespace;
}

auto CppEmitter::param_types_of(const Sym* fn) -> std::vector<Type*> {
    std::vector<Type*> out;
    const nodes::FunctionParameters* ps = params_of(fn ? fn->decl : nullptr);
    if (!ps) return out;
    Inst* I = m_inst.owning(const_cast<Sym*>(fn));
    EnvScope env(m_types, I ? &I->env : nullptr);
    for (const nodes::FunctionParameter* p : ps->m_params) out.push_back(m_types.canonicalize(p->get_type()));
    return out;
}

const nodes::FunctionParameters* CppEmitter::params_of(const nodes::ASTNode* d) {
    if (!d) return nullptr;
    switch (d->kind) {
        case K::FunctionDeclaration:         return static_cast<const nodes::FunctionDeclaration*>(d)->get_parameters();
        case K::OperatorFunctionDeclaration: return static_cast<const nodes::OperatorFunctionDeclaration*>(d)->get_parameters();
        case K::ConstructorDeclaration:      return static_cast<const nodes::ConstructorDeclaration*>(d)->get_parameters();
        default:                             return nullptr;
    }
}

std::string CppEmitter::signature_mangle(const Sym* fn) {
    std::string out;
    for (Type* t : param_types_of(fn)) { out += "_"; out += type_mangle(t); }
    if (fn && fn->decl) {
        const modifiers::FunctionQualifiers* q = nullptr;
        if (fn->decl->kind == K::FunctionDeclaration) q = &static_cast<const nodes::FunctionDeclaration*>(fn->decl)->qualifiers();
        if (fn->decl->kind == K::OperatorFunctionDeclaration) q = &static_cast<const nodes::OperatorFunctionDeclaration*>(fn->decl)->qualifiers();
        if (q && q->has(FQ::Const)) out += "_c";
    }
    return out;
}

std::string CppEmitter::operator_word(const nodes::OperatorFunctionDeclaration* op) {
    if (op->is_conversion()) return "conv_" + type_mangle(m_types.canonicalize(op->get_conversion_type()));
    return "op_" + sanitize(nodes::overloadable_operator_name(op->get_overload()));
}

std::string CppEmitter::operator_word_named(const nodes::OperatorFunctionDeclaration* op) {
    std::string sym = std::string(nodes::overloadable_operator_name(op->get_overload()));
    static const std::map<std::string, std::string> names = {
        {"+","add"},{"-","sub"},{"*","mul"},{"/","div"},{"%","mod"},{"**","pow"},{"==","eq"},{"!=","ne"},{"<","lt"},{">","gt"},{"<=","le"},{">=","ge"},
        {"&","band"},{"|","bor"},{"^","bxor"},{"~","bnot"},{"!","not"},{"&&","land"},{"||","lor"},{"<<","shl"},{">>","shr"},{"=","assign"},
        {"+=","add_assign"},{"-=","sub_assign"},{"*=","mul_assign"},{"/=","div_assign"},{"%=","mod_assign"},{"**=","pow_assign"},{"&=","band_assign"},
        {"|=","bor_assign"},{"^=","bxor_assign"},{"<<=","shl_assign"},{">>=","shr_assign"},{"++","inc"},{"--","dec"},{"[]","index"},{"()","call"},{"->","arrow"}
    };
    if (op->is_conversion()) return "conv_" + type_mangle(m_types.canonicalize(op->get_conversion_type()));
    auto it = names.find(sym);
    return "op_" + (it == names.end() ? sanitize(sym) : it->second);
}

auto CppEmitter::definition_of(const Sym* fn) -> const Sym* {
    for (int hops = 0; fn && fn->extern_target && hops < 64; ++hops) fn = fn->extern_target;
    if (!fn || !fn->decl || fn->decl->kind != K::FunctionDeclaration) return fn;
    auto* fd = static_cast<const nodes::FunctionDeclaration*>(fn->decl);
    if (fd->has_body() && fd->symbol && fd->symbol != fn) return fd->symbol;
    if (fd->has_body() || !fn->owner) return fn;
    const std::string sig = signature_mangle(fn);

    for (const Sym* o = fn->owner->find_local(fn->name); o; o = o->next_overload) {
        if (o == fn || !o->decl || o->decl->kind != K::FunctionDeclaration) continue;
        if (!static_cast<const nodes::FunctionDeclaration*>(o->decl)->has_body()) continue;
        if (signature_mangle(o) == sig) return o;
    }

    return fn;
}

std::string CppEmitter::function_name(const Sym* fn) {
    fn = definition_of(fn);
    if (auto it = m_name_cache.find(fn); it != m_name_cache.end()) return it->second;
    std::string base;
    Inst* I = m_inst.owning(const_cast<Sym*>(fn));

    if (fn->decl && fn->decl->kind == K::OperatorFunctionDeclaration) {
        base = operator_word_named(static_cast<const nodes::OperatorFunctionDeclaration*>(fn->decl));
    } else {
        base = owner_record_of(const_cast<Sym*>(fn)) ? "m_" + sanitize(fn->name) : "f_" + sanitize(qualified_name(fn));
    }

    std::string name = base + "_" + signature_mangle(fn);
    if (I && I->sym == fn) name += "_I_" + mangle_args(I->args);
    if (!owner_record_of(const_cast<Sym*>(fn))) name = unique_global(name);
    m_name_cache[fn] = name;
    return name;
}

std::string CppEmitter::global_var_name(const Sym* s) {
    if (m_track_globals && s) m_used_globals.insert(s);
    if (auto it = m_name_cache.find(s); it != m_name_cache.end()) return it->second;
    std::string n = unique_global("g_" + sanitize(qualified_name(s)));
    m_name_cache[s] = n;
    return n;
}

std::string CppEmitter::enum_name(const Sym* s) {
    if (auto it = m_name_cache.find(s); it != m_name_cache.end()) return it->second;
    std::string n = unique_global("E_" + sanitize(qualified_name(s)));
    m_name_cache[s] = n;
    return n;
}

auto CppEmitter::value_type(Type* t) -> Type* {
    if (!t) return nullptr;
    if (t->is_reference()) t = static_cast<semantics::ReferenceType*>(t)->referent();
    return m_types.strip_cv(t);
}

std::string CppEmitter::builtin_cpp(semantics::BuiltinType* b, const nodes::ASTNode* at) {
    const unsigned bits = bit_width_of_rank(b->width());

    switch (b->base()) {
        case BK::Int:     return std::string("walnut_rt::") + (b->is_unsigned() ? "u" : "i") + std::to_string(bits);
        case BK::Float:   return std::string("walnut_rt::") + (b->is_unsigned() ? "uf" : "f") + std::to_string(bits);
        case BK::Bool:    return "bool";
        case BK::Char:    return "char32_t";
        case BK::String:  return "std::string";
        case BK::Text:    return "std::string";
        case BK::Void:    return "void";
        case BK::Dynamic: return "walnut_rt::dynamic";
        case BK::Auto:    fail_at(at, "a type could not be deduced for code generation"); return "int";
    }
    return "int";
}

std::string CppEmitter::cpp_type(Type* t, const nodes::ASTNode* at) {
    if (!t) { fail_at(at, "an expression has no type"); return "int"; }
    const std::string cv = t->cv().is_const ? "const " : "";

    switch (t->kind()) {
        case semantics::TypeKind::Builtin:   return cv + builtin_cpp(static_cast<semantics::BuiltinType*>(t), at);
        case semantics::TypeKind::Pointer:   return cpp_type(static_cast<semantics::PointerType*>(t)->pointee(), at) + "*" + (t->cv().is_const ? " const" : "");
        case semantics::TypeKind::Reference: {
            auto* r = static_cast<semantics::ReferenceType*>(t);
            if (Type* ra = m_types.strip_cv(r->referent()); ra && ra->is_array() && !static_cast<semantics::ArrayType*>(ra)->extent()) {
                return "walnut_rt::array_ref<" + cpp_type(m_types.strip_cv(static_cast<semantics::ArrayType*>(ra)->element()), at) + ">";
            }
            return cpp_type(r->referent(), at) + (r->ref_qual() == semantics::RefQual::RValue ? "&&" : "&");
        }
        case semantics::TypeKind::Array: {
            auto* a = static_cast<semantics::ArrayType*>(t);
            if (a->extent()) return cv + "std::array<" + cpp_type(a->element(), at) + ", " + std::to_string(*a->extent()) + ">";
            return cv + "std::vector<" + cpp_type(m_types.strip_cv(a->element()), at) + ">";
        }
        case semantics::TypeKind::Record: {
            RecordInfo* r = record_info_of(t);
            if (!r) { fail_at(at, "record type '" + type_text(t) + "' has no generated definition"); return "int"; }
            return cv + r->cname;
        }
        case semantics::TypeKind::Enum:      return cv + enum_name(static_cast<semantics::EnumType*>(t)->decl());
        case semantics::TypeKind::Null:      return "std::nullptr_t";
        case semantics::TypeKind::SpecialValue: return "walnut_rt::special_value";
        case semantics::TypeKind::Closure:   return "auto";
        case semantics::TypeKind::Coroutine: {
            auto* c = static_cast<semantics::CoroutineType*>(t);
            if (c->is_generator()) m_uses_coroutines = true;
            return cv + (c->is_generator() ? "walnut_rt::generator<" : "walnut_rt::task<") + cpp_type(m_types.strip_cv(c->value()), at) + ">";
        }
        case semantics::TypeKind::Function: {
            auto* f = static_cast<semantics::FunctionType*>(t);
            std::string s = "std::function<" + cpp_type(f->ret(), at) + "(";
            for (std::size_t i = 0; i < f->params().size(); ++i) { if (i) s += ", "; s += cpp_type(f->params()[i], at); }
            return s + ")>";
        }
        case semantics::TypeKind::Variant: {
            auto* v = static_cast<semantics::VariantType*>(t);
            std::string s = cv + "std::variant<";
            for (std::size_t i = 0; i < v->alternatives().size(); ++i) { if (i) s += ", "; s += cpp_type(v->alternatives()[i], at); }
            return s + ">";
        }
        default:
            fail_at(at, "type '" + type_text(t) + "' cannot be generated");
            return "int";
    }
}

std::string CppEmitter::convert(const std::string& code, Type* from, Type* to, const nodes::ASTNode* at, bool explicit_cast) {
    Type* f = value_type(from);
    Type* t = value_type(to);
    if (!t || !f) return code;
    if (f == t) return code;
    if (t->is_error() || f->is_error()) return code;

    if (t->kind() == semantics::TypeKind::Variant && f->kind() != semantics::TypeKind::Variant) {
        auto* vt = static_cast<semantics::VariantType*>(t);
        const int i = semantics::variant_alternative_for(f, vt, m_types);
        if (i >= 0) return cpp_type(t, at) + "(std::in_place_index<" + std::to_string(i) + ">, " + convert(code, f, vt->alternatives()[static_cast<std::size_t>(i)], at, false) + ")";
    }

    if (is_dynamic(t) && f->is_function()) {
        auto* ft = static_cast<semantics::FunctionType*>(f);
        std::string targs = cpp_type(ft->ret(), at);
        for (Type* p : ft->params()) targs += ", " + cpp_type(p, at);
        return "walnut_rt::make_dynamic_function<" + targs + ">(" + code + ")";
    }

    if (is_dynamic(t)) return "walnut_rt::dynamic(" + code + ")";
    if (is_dynamic(f)) return "walnut_rt::from_dynamic<" + cpp_type(t, at) + ">(" + code + ")";

    auto* fb = as_builtin(f);
    auto* tb = as_builtin(t);

    if (f->is_special_value() && tb) return "walnut_rt::cast<" + cpp_type(t, at) + ">(" + code + ")";

    if (fb && tb) {
        if ((fb->base() == BK::String || fb->base() == BK::Text) && (tb->base() == BK::String || tb->base() == BK::Text)) return code;
        return "walnut_rt::cast<" + cpp_type(t, at) + ">(" + code + ")";
    }

    if (f->is_null() && t->is_pointer()) return code;
    if (f->is_array() && t->is_array() && !static_cast<semantics::ArrayType*>(t)->extent()) return "walnut_rt::to_vector(" + code + ")";
    if (f->is_pointer() && tb && tb->is_bool()) return "(" + code + " != nullptr)";
    if (f->is_enum() && tb) return "walnut_rt::cast<" + cpp_type(t, at) + ">(walnut_rt::i64(static_cast<long long>(" + code + ")))";
    if (t->is_enum() && fb) return "static_cast<" + cpp_type(t, at) + ">(walnut_rt::to_ll(" + code + "))";
    if (t->is_enum() && f->is_enum()) return "static_cast<" + cpp_type(t, at) + ">(static_cast<long long>(" + code + "))";
    if (f->is_pointer() && t->is_pointer()) return explicit_cast ? "static_cast<" + cpp_type(t, at) + ">(" + code + ")" : code;

    if (f->kind() == semantics::TypeKind::Closure && t->kind() == semantics::TypeKind::Function) {
        if (nodes::LambdaExpression* spec = closure_spec_for(f, static_cast<semantics::FunctionType*>(t))) return closure_adapter(code, spec, static_cast<semantics::FunctionType*>(t), at);
    }

    if (f->is_record() && t->kind() == semantics::TypeKind::Function && m_types.callable_hook) {
        if (Sym* op = m_types.callable_hook(f, t)) return functor_adapter(code, op, static_cast<semantics::FunctionType*>(t), at);
    }

    if (f->is_record() && !t->is_record()) {
        Sym* chosen = nullptr;
        if (semantics::rank_user_defined(f, t, m_types, true, &chosen) != semantics::ConversionRank::None && chosen) {
            return "(" + code + ")." + function_name(chosen) + "()";
        }
    }

    if (f->is_record() && t->is_record()) {
        Sym* chosen = nullptr;
        if (semantics::rank_user_defined(f, t, m_types, true, &chosen) != semantics::ConversionRank::None && chosen) {
            return "(" + code + ")." + function_name(chosen) + "()";
        }
        return code;
    }

    return code;
}

nodes::LambdaExpression* CppEmitter::closure_spec_for(Type* closure, semantics::FunctionType* sig) {
    auto* gl = static_cast<nodes::LambdaExpression*>(static_cast<semantics::ClosureType*>(closure)->lambda());
    std::vector<Type*> key;
    for (Type* p : sig->params()) key.push_back(value_type(p));
    for (std::size_t i = 0; i < gl->spec_keys.size(); ++i) if (gl->spec_keys[i] == key && !gl->spec_failed[i]) return gl->specializations[i];
    return nullptr;
}

std::string CppEmitter::closure_adapter(const std::string& code, nodes::LambdaExpression* spec, semantics::FunctionType* sig, const nodes::ASTNode* at) {
    Type* st = value_type(type_of(spec));
    auto* sf = (st && st->kind() == semantics::TypeKind::Function) ? static_cast<semantics::FunctionType*>(st) : nullptr;
    if (!sf) return code;
    const std::string obj = "walnut_f" + std::to_string(m_temp++);
    std::string out = "[" + obj + " = " + code + "](";
    std::string args;

    for (std::size_t i = 0; i < sig->params().size(); ++i) {
        const std::string a = "walnut_p" + std::to_string(i);
        if (i) { out += ", "; args += ", "; }
        out += cpp_type(sig->params()[i], at) + " " + a;
        args += i < sf->params().size() ? convert(a, sig->params()[i], sf->params()[i], at) : a;
    }

    const bool void_ret = is_void(sig->ret());
    out += ") mutable -> " + cpp_type(sig->ret(), at) + " { ";
    const std::string call = obj + "(" + args + ")";
    out += void_ret ? call + "; }" : "return " + convert(call, sf->ret(), sig->ret(), at) + "; }";
    return out;
}

std::string CppEmitter::functor_adapter(const std::string& code, Sym* op, semantics::FunctionType* sig, const nodes::ASTNode* at) {
    std::vector<Type*> params = param_types_of(op);
    Type* ret = return_type_of(op);
    const std::string obj = "walnut_f" + std::to_string(m_temp++);
    std::string out = "[" + obj + " = " + code + "](";
    std::string args;

    for (std::size_t i = 0; i < sig->params().size(); ++i) {
        const std::string a = "walnut_p" + std::to_string(i);
        if (i) { out += ", "; args += ", "; }
        out += cpp_type(sig->params()[i], at) + " " + a;
        args += i < params.size() ? convert(a, sig->params()[i], params[i], at) : a;
    }

    const bool void_ret = is_void(sig->ret());
    out += ") mutable -> " + cpp_type(sig->ret(), at) + " { ";
    const std::string call = obj + "." + function_name(op) + "(" + args + ")";
    out += void_ret ? call + "; }" : "return " + convert(call, ret, sig->ret(), at) + "; }";
    return out;
}

std::string CppEmitter::folded_or(nodes::VariableDeclaration* v, Type* t, bool prefer_constant) {
    using RM = modifiers::RawModifiers;
    Type* vt = value_type(t);
    auto* b = as_builtin(vt);
    const bool scalar = (b && (b->base() == BK::Int || b->base() == BK::Float || b->base() == BK::Bool || b->base() == BK::Char)) || (vt && vt->is_enum());

    const bool constant_required = v->get_type_info().modifiers.has(RM::Constexpr) || v->get_type_info().modifiers.has(RM::Constinit);

    if (constant_required && !scalar && v->m_initializer) {
        const std::size_t draws = m_eval.random_draws();
        semantics::ConstValue cv;
        {
            semantics::ConstEvaluator::RandomPermit permit(m_eval, v);
            cv = m_eval.eval(v->m_initializer);
        }
        if (draws != m_eval.random_draws() && cv.is_string()) return constant_value_code(cv, vt, v);
    }

    if (scalar && (prefer_constant || constant_required)) {
        semantics::ConstValue cv;
        if (constant_required) {
            semantics::ConstEvaluator::RandomPermit permit(m_eval, v);
            cv = m_eval.eval(v->m_initializer);
        } else {
            cv = m_eval.eval(v->m_initializer);
        }
        if (cv.ok() && !cv.is_bool() == !(b && b->base() == BK::Bool)) {
            if (vt->is_enum() && cv.is_int()) return "static_cast<" + cpp_type(vt, v) + ">(" + cv.i->to_string() + "LL)";
            if (!vt->is_enum()) return constant_value_code(cv, vt, v);
        }
    }

    return conv(v->m_initializer, t, v);
}

std::string CppEmitter::conv(nodes::ASTNode* n, Type* to, const nodes::ASTNode* at) {
    if (n && n->kind == K::BraceInitializerList) return brace_list(static_cast<nodes::BraceInitializerList*>(n), to, at);
    Type* t = value_type(to);
    auto* tb = as_builtin(t);

    if (n && tb && tb->base() == BK::Float) {
        bool negative = false;
        nodes::ASTNode* core = n;

        if (core->kind == K::UnaryExpression && static_cast<nodes::UnaryExpression*>(core)->op == TK::Minus && !static_cast<nodes::UnaryExpression*>(core)->resolved) {
            negative = true;
            core = static_cast<nodes::UnaryExpression*>(core)->operand;
        }

        if (core && core->kind == K::Literal) {
            auto* lit = static_cast<nodes::Literal*>(core);
            const tokenizing::NumericLiteral num = tokenizing::parse_numeric_literal(lit->value);
            if ((lit->get_kind() == TK::Float || lit->get_kind() == TK::Integer) && !num.has_suffix && num.base == 10 && num.error.empty()) return float_literal((negative ? "-" : "") + num.body, t, at);
        }
    }

    return convert(expr(n), type_of(n), to, at);
}

std::string CppEmitter::pool_constant(const std::string& type, const std::string& text) {
    const std::string key = type + "|" + text;
    if (auto it = m_const_pool.find(key); it != m_const_pool.end()) return it->second;
    std::string name = "k_" + std::to_string(m_const_pool.size());
    m_const_pool[key] = name;
    m_consts << "static const " << type << " " << name << " = " << type << "(std::string(\"" << text << "\"));\n";
    return name;
}

std::string CppEmitter::cpp_string_literal(const std::string& bytes) {
    std::string out = "std::string(\"";
    for (unsigned char c : bytes) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c >= 32 && c < 127) out += static_cast<char>(c);
        else { char buf[8]; std::snprintf(buf, sizeof buf, "\\x%02x\"\"", c); out += buf; }
    }
    out += "\", " + std::to_string(bytes.size()) + ")";
    return out;
}

std::string CppEmitter::int_literal(const WideInt& v, Type* t, const nodes::ASTNode* at) {
    const std::string type = cpp_type(t, at);
    const std::string digits = v.to_string();
    const bool fits = digits.size() < 18;
    if (fits) return type + "(" + digits + "LL)";
    return pool_constant(type, digits);
}

std::string CppEmitter::constant_value_code(const semantics::ConstValue& v, Type* t, const nodes::ASTNode* at) {
    Type* vt = value_type(t);
    if (v.is_bool()) return v.b ? "true" : "false";
    if (v.is_int()) {
        if (vt && vt->is_builtin() && as_builtin(vt)->base() == BK::Float) {
            return "walnut_rt::cast<" + cpp_type(vt, at) + ">(" + int_literal(*v.i, m_types.builtin(BK::Int, parser_types::LengthModifier::LongLong), at) + ")";
        }
        if (vt && vt->is_builtin() && as_builtin(vt)->base() == BK::Char) return "char32_t(" + v.i->to_string() + "u)";
        return int_literal(*v.i, vt, at);
    }
    if (v.is_float()) {
        std::ostringstream os;
        os.precision(1000);
        os << *v.f;
        return float_literal(os.str(), vt, at);
    }
    if (v.is_special()) {
        if (vt && vt->is_builtin()) return "walnut_rt::cast<" + cpp_type(vt, at) + ">(" + special_code(v.sp) + ")";
        return special_code(v.sp);
    }
    if (v.is_string()) {
        if (vt && vt->is_builtin() && as_builtin(vt)->base() == BK::Text) return cpp_type(vt, at) + "(" + cpp_string_literal(*v.s) + ")";
        return cpp_string_literal(*v.s);
    }
    return "0";
}

std::string CppEmitter::special_code(semantics::SpecialKind k) {
    switch (k) {
        case semantics::SpecialKind::PosInf:        return "walnut_rt::special_value(walnut_rt::special_kind::pos_inf)";
        case semantics::SpecialKind::NegInf:        return "walnut_rt::special_value(walnut_rt::special_kind::neg_inf)";
        case semantics::SpecialKind::Nan:           return "walnut_rt::special_value(walnut_rt::special_kind::nan)";
        case semantics::SpecialKind::Undefined:     return "walnut_rt::special_value(walnut_rt::special_kind::undefined)";
        case semantics::SpecialKind::Indeterminate: return "walnut_rt::special_value(walnut_rt::special_kind::indeterminate)";
    }
    return "walnut_rt::special_value()";
}

std::string CppEmitter::literal(nodes::Literal* lit) {
    Type* t = type_of(lit);

    switch (lit->get_kind()) {
        case TK::Integer: {
            semantics::ConstValue v = m_eval.eval(lit);
            if (v.is_float()) return constant_value_code(v, t, lit);
            if (!v.is_int()) { fail_at(lit, "integer literal could not be evaluated"); return "0"; }
            return int_literal(*v.i, t, lit);
        }
        case TK::Float:          return float_literal(tokenizing::parse_numeric_literal(lit->value).body, t, lit);
        case TK::InfinityKeyword:      return special_code(semantics::SpecialKind::PosInf);
        case TK::NanKeyword:           return special_code(semantics::SpecialKind::Nan);
        case TK::UndefinedKeyword:     return special_code(semantics::SpecialKind::Undefined);
        case TK::IndeterminateKeyword: return special_code(semantics::SpecialKind::Indeterminate);
        case TK::True:           return "true";
        case TK::False:          return "false";
        case TK::NullptrKeyword: return "nullptr";
        case TK::ThisKeyword:    return "this";

        case TK::String:
        case TK::TextLiteral: {
            std::string bytes;
            tokenizing::EscapeError err = tokenizing::EscapeError::None;
            if (!tokenizing::decode_literal(lit->value, bytes, err)) { fail_at(lit, "invalid string literal"); return "std::string()"; }
            return cpp_string_literal(bytes);
        }

        case TK::Character: {
            std::uint32_t cp = 0;
            tokenizing::EscapeError err = tokenizing::EscapeError::None;
            if (!tokenizing::decode_char(lit->value, cp, err)) { fail_at(lit, "invalid character literal"); return "char32_t(0)"; }
            return "char32_t(" + std::to_string(cp) + "u)";
        }

        default:
            unsupported(lit, "this literal");
            return "0";
    }
}

std::string CppEmitter::symbol_ref(Sym* s, const nodes::ASTNode* at) {
    if (!s) { fail_at(at, "unresolved name"); return "0"; }

    switch (s->kind) {
        case SK::Variable:
        case SK::Parameter: {
            for (int hops = 0; s->extern_target && hops < 64; ++hops) s = s->extern_target;
            if (nodes::VariableDeclaration* iv = inline_variable(s->decl)) return "(" + folded_or(iv, declared_type(iv), true) + ")";
            const auto k = owner_kind(s);
            if (k == semantics::Scope::Kind::Module || k == semantics::Scope::Kind::Namespace) {
                if (is_reference_global(s) || is_binding_symbol(s)) return "(*" + global_var_name(s) + ")";
                if (is_closure_global(s)) return global_var_name(s);
                return global_var_name(s) + ".get()";
            }

            if (k == semantics::Scope::Kind::Record) {
                RecordInfo* r = owner_record_of(s);
                const bool is_static = (s->decl && s->decl->kind == K::VariableDeclaration && static_cast<nodes::VariableDeclaration*>(s->decl)->get_type_info().modifiers.has(RM::Static))
                                    || (s->decl && s->decl->kind == K::ArrayDeclaration && static_cast<nodes::ArrayDeclaration*>(s->decl)->get_array_modifiers().has(RM::Static));
                if (is_static && r) return r->cname + "::" + local_name(s->name);
                if (is_lazy_field(s)) return "this->lz_" + local_name(s->name) + "()";
                return "this->" + local_name(s->name);
            }

            return local_name(s->name);
        }

        case SK::EnumConstant: {
            Sym* en = s->owner ? s->owner->owner_symbol : nullptr;
            if (!en) { fail_at(at, "enum constant without enum"); return "0"; }
            return enum_name(en) + "::" + local_name(s->name);
        }

        case SK::Function: {
            if (is_local_function(s)) return function_name(s);
            if (owner_record_of(s)) { unsupported(at, "taking a member function as a value"); return "0"; }
            return "&" + function_name(s);
        }

        case SK::TemplateParam: {
            semantics::ConstValue v = m_eval.eval(const_cast<nodes::ASTNode*>(at));
            if (!v.ok()) { fail_at(at, "template parameter '" + std::string(s->name) + "' has no value here"); return "0"; }
            return constant_value_code(v, type_of(at), at);
        }

        default:
            unsupported(at, "this kind of name reference");
            return "0";
    }
}

bool CppEmitter::needs_volatile_access(const nodes::ASTNode* e) {
    if (!e) return false;

    switch (e->kind) {
        case K::Identifier:
        case K::QualifiedIdentifier:
        case K::SubscriptExpression:
        case K::MemberAccessExpression:
            break;
        case K::DereferenceExpression:
            if (static_cast<const nodes::DereferenceExpression*>(e)->resolved) return false;
            break;
        default:
            return false;
    }

    Type* t = type_of(e);
    if (t && t->is_reference()) t = static_cast<semantics::ReferenceType*>(t)->referent();
    return t && t->kind() != semantics::TypeKind::Function && t->cv().is_volatile;
}

std::string CppEmitter::expr(nodes::ASTNode* e) {
    const bool saved_checked = m_checked_arith;
    if (e) m_checked_arith = nodes::in_safe_code(e);
    std::string s = expr_unwrapped(e);
    m_checked_arith = saved_checked;
    if (needs_volatile_access(e)) return "walnut_rt::volatile_access(" + s + ")";
    return s;
}

std::string CppEmitter::expr_unwrapped(nodes::ASTNode* e) {
    if (!e) return "";

    switch (e->kind) {
        case K::Literal: return literal(static_cast<nodes::Literal*>(e));

        case K::Identifier:
            return symbol_ref(static_cast<nodes::Identifier*>(e)->resolved, e);

        case K::QualifiedIdentifier:
            return symbol_ref(static_cast<nodes::QualifiedIdentifier*>(e)->resolved, e);

        case K::BinaryExpression:  return binary(static_cast<nodes::BinaryExpression*>(e));
        case K::UnaryExpression:   return unary(static_cast<nodes::UnaryExpression*>(e));

        case K::BitwiseNotExpression: {
            auto* n = static_cast<nodes::BitwiseNotExpression*>(e);
            if (n->resolved) return call_operator_symbol(n->resolved, { n->operand }, e);
            Type* rt = value_type(type_of(e));
            return "walnut_rt::op_bitnot(" + conv(n->operand, rt, e) + ")";
        }

        case K::DereferenceExpression: {
            auto* d = static_cast<nodes::DereferenceExpression*>(e);
            if (d->resolved) return call_operator_symbol(d->resolved, { d->operand }, e);
            return "(*walnut_rt::safety::checked(" + expr(d->operand) + "))";
        }

        case K::ReferenceExpression:
            return "(&" + expr(static_cast<nodes::ReferenceExpression*>(e)->operand) + ")";

        case K::TernaryExpression: {
            auto* t = static_cast<nodes::TernaryExpression*>(e);
            Type* rt = type_of(e);
            return "(" + truthy(t->condition) + " ? " + conv(t->true_branch, rt, e) + " : " + conv(t->false_branch, rt, e) + ")";
        }

        case K::CallExpression:          return call(static_cast<nodes::CallExpression*>(e));
        case K::SubscriptExpression:     return subscript(static_cast<nodes::SubscriptExpression*>(e));
        case K::MemberAccessExpression:  return member_access(static_cast<nodes::MemberAccessExpression*>(e));

        case K::CastExpression: {
            auto* c = static_cast<nodes::CastExpression*>(e);
            Type* to = type_of(e);
            using CK = nodes::CastExpression::CastKind;
            if (c->cast_kind == CK::Is || c->cast_kind == CK::As) return type_test(c);
            if (c->interpolation) return interpolated_text(c);
            if (c->cast_kind == CK::Reinterpret) {
                Type* from_t = value_type(type_of(c->operand));
                Type* to_t = value_type(to);
                if (from_t && from_t->is_pointer() && to_t && to_t->is_builtin()) return "walnut_rt::from_native<" + cpp_type(to_t, e) + ">(static_cast<long long>(reinterpret_cast<std::intptr_t>(" + expr(c->operand) + ")))";
                if (to_t && to_t->is_pointer() && from_t && from_t->is_builtin()) return "reinterpret_cast<" + cpp_type(to, e) + ">(static_cast<std::intptr_t>(walnut_rt::to_native<long long>(" + expr(c->operand) + ")))";
                return "reinterpret_cast<" + cpp_type(to, e) + ">(" + expr(c->operand) + ")";
            }
            if (c->cast_kind == CK::Const)       return "const_cast<" + cpp_type(m_types.canonicalize(c->get_target()), e) + ">(" + expr(c->operand) + ")";
            if (c->cast_kind == CK::Dynamic)     return "dynamic_cast<" + cpp_type(m_types.canonicalize(c->get_target()), e) + ">(" + expr(c->operand) + ")";
            if (c->cast_kind == CK::Static) {
                Type* target = m_types.canonicalize(c->get_target());
                if (target && target->is_reference()) return "static_cast<" + cpp_type(target, e) + ">(" + expr(c->operand) + ")";
            }
            if (c->conversion) return "(" + expr(c->operand) + ")." + function_name(c->conversion) + "()";
            return convert(expr(c->operand), type_of(c->operand), to, e, true);
        }

        case K::NewExpression: {
            auto* n = static_cast<nodes::NewExpression*>(e);
            Type* allocated = m_types.canonicalize(n->type);
            const std::string tc = cpp_type(allocated, e);

            if (n->alloc) {
                std::vector<Type*> ps = param_types_of(n->alloc);
                const std::string size_type = ps.empty() ? std::string("walnut_rt::i64") : cpp_type(m_types.strip_cv(value_type(ps[0])), e);
                const std::string fn = owner_record_of(n->alloc) ? owner_record_of(n->alloc)->cname + "::" + function_name(n->alloc) : function_name(n->alloc);
                const std::string alloc = "[](long long bytes) -> void* { return " + fn + "(walnut_rt::from_native<" + size_type + ">(bytes)); }";
                if (n->is_array) return "walnut_rt::custom_new_array<" + tc + ">(walnut_rt::to_ll(" + expr(n->array_size) + "), " + alloc + ")";
                return "walnut_rt::custom_new<" + tc + ">(" + alloc + (n->args.empty() && !n->ctor ? std::string() : ", " + call_args(n->ctor, n->args, e)) + ")";
            }

            const std::string shown = "\"" + new_type_text(n, tc) + "\"";
            if (n->is_array) return "walnut_rt::safety::new_array<" + tc + ">(walnut_rt::to_ll(" + expr(n->array_size) + "), " + shown + ", __FILE__, __LINE__)";
            return "walnut_rt::safety::track_new(new " + tc + "(" + call_args(n->ctor, n->args, e) + "), " + shown + ", __FILE__, __LINE__)";
        }

        case K::DeleteExpression: {
            auto* d = static_cast<nodes::DeleteExpression*>(e);

            if (d->dealloc) {
                const std::string fn = owner_record_of(d->dealloc) ? owner_record_of(d->dealloc)->cname + "::" + function_name(d->dealloc) : function_name(d->dealloc);
                const std::string release = "[](void* memory) { " + fn + "(memory); }";
                return std::string(d->is_array ? "walnut_rt::custom_delete_array(" : "walnut_rt::custom_delete(") + expr(d->operand) + ", " + release + ")";
            }

            return std::string(d->is_array ? "walnut_rt::safety::delete_array(" : "walnut_rt::safety::delete_one(") + expr(d->operand) + ", __FILE__, __LINE__)";
        }

        case K::BraceConstructExpression: {
            auto* b = static_cast<nodes::BraceConstructExpression*>(e);
            Type* t = type_of(e);
            std::vector<nodes::ASTNode*> els = b->m_init ? b->m_init->m_elements : std::vector<nodes::ASTNode*>{};
            if (b->ctor) return invoke_with_args(cpp_type(t, e), b->ctor, els, e);
            return cpp_type(t, e) + "{" + aggregate_args(t, els, e) + "}";
        }

        case K::ThrowExpression: {
            auto* t = static_cast<nodes::ThrowExpression*>(e);
            return t->operand ? "(throw " + expr(t->operand) + ")" : "(throw)";
        }

        case K::NoexceptExpression: {
            auto* n = static_cast<nodes::NoexceptExpression*>(e);
            return n->is_nothrow ? "true" : "false";
        }

        case K::DiscardExpression:
            return "static_cast<void>(" + expr(static_cast<nodes::DiscardExpression*>(e)->operand) + ")";

        case K::AwaitExpression:
            return "(" + expr(static_cast<nodes::AwaitExpression*>(e)->operand) + ").get()";

        case K::CoYieldExpression: {
            auto* y = static_cast<nodes::CoYieldExpression*>(e);
            if (!m_yield_type) { fail_at(e, "'co_yield' outside a generator"); return "0"; }
            return "co_yield " + conv(y->operand, m_yield_type, e);
        }

        case K::LambdaExpression: return lambda(static_cast<nodes::LambdaExpression*>(e));
        case K::FoldExpression:   return fold(static_cast<nodes::FoldExpression*>(e));

        case K::TypeQuery: {
            auto* q = static_cast<nodes::TypeQueryExpression*>(e);

            if (q->op == nodes::TypeQueryExpression::Op::Typeid) {
                if (q->operand && q->queried && is_dynamic(value_type(q->queried))) {
                    if (q->operand->is_value()) return "std::string((" + expr(q->operand->value) + ").type_name())";
                    Sym* vs = q->operand->type.resolved;
                    if (vs && (vs->kind == SK::Variable || vs->kind == SK::Parameter)) return "std::string((" + symbol_ref(vs, e) + ").type_name())";
                }
                if (q->type_name) return cpp_string_literal(*q->type_name);
                unsupported(e, "typeid on this operand");
                return "std::string()";
            }

            semantics::ConstValue v = m_eval.eval(e);
            if (v.ok()) return constant_value_code(v, type_of(e), e);

            if ((q->op == nodes::TypeQueryExpression::Op::Sizeof || q->op == nodes::TypeQueryExpression::Op::Alignof) && q->queried) {
                Type* qt = m_types.strip_cv(m_types.apply_subst(q->queried));
                if (qt && !qt->is_dependent()) return "walnut_rt::from_native<" + cpp_type(value_type(type_of(e)), e) + ">(" + (q->op == nodes::TypeQueryExpression::Op::Sizeof ? "sizeof(" : "alignof(") + cpp_type(qt, e) + "))";
            }

            if (q->op == nodes::TypeQueryExpression::Op::Countof && q->operand) {
                std::string target;
                if (q->operand->is_value()) target = expr(q->operand->value);
                else if (q->operand->type.resolved && (q->operand->type.resolved->kind == SK::Variable || q->operand->type.resolved->kind == SK::Parameter)) target = symbol_ref(q->operand->type.resolved, e);
                if (!target.empty()) return "walnut_rt::count_of(" + target + ")";
            }

            unsupported(e, std::string(q->op_name()) + " on this operand");
            return "0";
        }

        case K::TemplateInstantiation: {
            semantics::ConstValue v = m_eval.eval(e);
            if (v.ok()) return constant_value_code(v, type_of(e), e);
            if (const int cr = static_cast<nodes::TemplateInstantiation*>(e)->concept_result; cr >= 0) return cr ? "true" : "false";
            if (Sym* vi = static_cast<nodes::TemplateInstantiation*>(e)->var_instance) {
                if (RecordInfo* r = owner_record_of(vi)) return r->cname + "::" + function_name(vi) + "()";
                return function_name(vi) + "()";
            }
            if (Sym* fi = static_cast<nodes::TemplateInstantiation*>(e)->fn_instance) return symbol_ref(fi, e);
            unsupported(e, "a template instantiation used as a value");
            return "0";
        }

        default: {
            semantics::ConstValue v = m_eval.eval(e);
            if (v.ok()) return constant_value_code(v, type_of(e), e);
            unsupported(e, std::string("this expression (") + node_kind_name(e) + ")");
            return "0";
        }
    }
}

const char* CppEmitter::node_kind_name(const nodes::ASTNode* n) {
    switch (n->kind) {
        case K::TypeQuery:          return "type query";
        case K::FoldExpression:     return "fold expression";
        case K::AwaitExpression:    return "await";
        case K::CoYieldExpression:  return "co_yield";
        case K::RequiresExpression: return "requires-expression";
        case K::BraceInitializerList: return "brace initializer list";
        default:                    return "expression";
    }
}

std::string CppEmitter::fold(nodes::FoldExpression* f) {
    if (f->lowered) return expr(f->lowered);
    Type* rt = value_type(type_of(f));
    if (!f->sequence) {
        if (is_void(rt)) return "static_cast<void>(0)";
        unsupported(f, "this fold expression");
        return "0";
    }

    const std::string n = std::to_string(m_temp++);
    const std::string seq = "walnut_fs" + n, idx = "walnut_fi" + n;
    const std::string el = cpp_type(f->element->bound_type, f) + " " + local_name(f->element->name) + " = " + seq + "[";
    std::string out = "([&]() -> " + (is_void(rt) ? std::string("void") : cpp_type(rt, f)) + " { auto&& " + seq + " = " + expr(f->sequence) + "; ";
    const std::string size = seq + ".size()";
    const std::string forward = "for (std::size_t " + idx + " = 0; " + idx + " < " + size + "; ++" + idx + ") { ";
    const std::string backward = "for (std::size_t " + idx + " = " + size + "; " + idx + "-- > 0;) { ";

    if (f->op == TK::Comma) {
        const std::string init = f->init ? "static_cast<void>(" + expr(f->init) + "); " : "";
        if (!f->right_to_left) out += init;
        out += forward + el + idx + "]; static_cast<void>(" + expr(f->first) + "); } ";
        if (f->right_to_left) out += init;
        return out + "}())";
    }

    const std::string acc = local_name(f->accumulator->name);
    out += cpp_type(rt, f) + " " + acc + "{}; ";

    if (f->init) {
        out += acc + " = " + conv(f->init, rt, f) + "; ";
        out += (f->right_to_left ? backward : forward) + el + idx + "]; " + acc + " = " + conv(f->step, rt, f) + "; } ";
        return out + "return " + acc + "; }())";
    }

    std::string empty;
    if (f->op == TK::LogicAnd) empty = "return true;";
    else if (f->op == TK::LogicOr) empty = "return false;";
    else empty = "walnut_rt::fail(\"fold over an empty sequence\");";
    out += "if (" + seq + ".empty()) { " + empty + " } ";

    if (!f->right_to_left) {
        out += "{ " + el + "0]; " + acc + " = " + conv(f->first, rt, f) + "; } ";
        out += "for (std::size_t " + idx + " = 1; " + idx + " < " + size + "; ++" + idx + ") { " + el + idx + "]; " + acc + " = " + conv(f->step, rt, f) + "; } ";
    } else {
        out += "{ " + el + size + " - 1]; " + acc + " = " + conv(f->first, rt, f) + "; } ";
        out += "for (std::size_t " + idx + " = " + size + " - 1; " + idx + "-- > 0;) { " + el + idx + "]; " + acc + " = " + conv(f->step, rt, f) + "; } ";
    }

    return out + "return " + acc + "; }())";
}

std::string CppEmitter::truthy(nodes::ASTNode* cond) {
    Type* t = value_type(type_of(cond));
    if (t && t->is_builtin() && as_builtin(t)->is_bool()) return expr(cond);
    if (t && t->is_record()) {
        Type* b = m_types.bool_();
        return convert(expr(cond), t, b, cond);
    }
    return "walnut_rt::truthy(" + expr(cond) + ")";
}

auto CppEmitter::common_type(Type* a, Type* b) -> Type* {
    a = value_type(a); b = value_type(b);
    if (!a) return b;
    if (!b) return a;
    if (a == b) return a;
    if (is_dynamic(a) || is_dynamic(b)) return m_types.dynamic_();
    if (a->is_enum() && !b->is_enum()) return b;
    if (b->is_enum() && !a->is_enum()) return a;
    if (semantics::rank_conversion(b, a, m_types) != semantics::ConversionRank::None) return a;
    if (semantics::rank_conversion(a, b, m_types) != semantics::ConversionRank::None) return b;
    return a;
}

const char* CppEmitter::arith_helper(TK op) const {
    if (m_checked_arith) {
        if (op == TK::Plus)     return "walnut_rt::checked::op_add";
        if (op == TK::Minus)    return "walnut_rt::checked::op_sub";
        if (op == TK::Asterisk) return "walnut_rt::checked::op_mul";
    }
    switch (op) {
        case TK::Plus:              return "walnut_rt::op_add";
        case TK::Minus:             return "walnut_rt::op_sub";
        case TK::Asterisk:          return "walnut_rt::op_mul";
        case TK::Slash:             return "walnut_rt::op_div";
        case TK::Percent:           return "walnut_rt::op_mod";
        case TK::DoubleAsterisk:    return "walnut_rt::op_pow";
        case TK::Ampersand:         return "walnut_rt::op_bitand";
        case TK::Pipe:              return "walnut_rt::op_bitor";
        case TK::Caret:             return "walnut_rt::op_bitxor";
        case TK::DoubleLessThan:    return "walnut_rt::op_shl";
        case TK::DoubleGreaterThan: return "walnut_rt::op_shr";
        default:                    return nullptr;
    }
}

auto CppEmitter::compound_base(TK op) -> TK {
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

bool CppEmitter::has_effects(const nodes::ASTNode* n) {
    if (!n) return false;

    switch (n->kind) {
        case K::Literal:
        case K::Identifier:
        case K::QualifiedIdentifier:
        case K::LambdaExpression:
            return false;

        case K::BinaryExpression: {
            auto* b = static_cast<const nodes::BinaryExpression*>(n);
            if (b->resolved || walnut::is_assignment_op(b->op)) return true;
            return has_effects(b->left) || has_effects(b->right);
        }

        case K::UnaryExpression: {
            auto* u = static_cast<const nodes::UnaryExpression*>(n);
            if (u->resolved || u->op == TK::DoublePlus || u->op == TK::DoubleMinus) return true;
            return has_effects(u->operand);
        }

        case K::TernaryExpression: {
            auto* t = static_cast<const nodes::TernaryExpression*>(n);
            return has_effects(t->condition) || has_effects(t->true_branch) || has_effects(t->false_branch);
        }

        case K::SubscriptExpression: {
            auto* s = static_cast<const nodes::SubscriptExpression*>(n);
            return s->resolved || has_effects(s->m_array) || has_effects(s->m_index);
        }

        case K::MemberAccessExpression: return has_effects(static_cast<const nodes::MemberAccessExpression*>(n)->m_object);

        case K::CastExpression: {
            auto* c = static_cast<const nodes::CastExpression*>(n);
            return c->conversion || has_effects(c->operand);
        }

        case K::DereferenceExpression: {
            auto* d = static_cast<const nodes::DereferenceExpression*>(n);
            return d->resolved || has_effects(d->operand);
        }

        case K::ReferenceExpression:  return has_effects(static_cast<const nodes::ReferenceExpression*>(n)->operand);
        case K::BitwiseNotExpression: return has_effects(static_cast<const nodes::BitwiseNotExpression*>(n)->operand);
        default:                      return true;
    }
}

bool CppEmitter::is_temporary_value(nodes::ASTNode* e) {
    if (!e) return false;
    if (e->kind != K::CallExpression && e->kind != K::BinaryExpression && e->kind != K::CastExpression) return false;
    Type* t = type_of(e);
    return t && !t->is_reference() && (value_type(t) && (value_type(t)->is_record() || value_type(t)->is_array()));
}

int CppEmitter::borrow_bit(nodes::ASTNode* e) {
    nodes::ASTNode* at = e;

    for (int depth = 0; at && depth < 64; ++depth) {
        nodes::ASTNode* object = nullptr;
        if (at->kind == K::SubscriptExpression) object = static_cast<nodes::SubscriptExpression*>(at)->m_array;
        else if (at->kind == K::MemberAccessExpression && !static_cast<nodes::MemberAccessExpression*>(at)->is_arrow() && !static_cast<nodes::MemberAccessExpression*>(at)->is_scope()) object = static_cast<nodes::MemberAccessExpression*>(at)->m_object;
        else if (at->kind == K::CallExpression && static_cast<nodes::CallExpression*>(at)->m_callee && static_cast<nodes::CallExpression*>(at)->m_callee->kind == K::MemberAccessExpression) {
            auto* m = static_cast<nodes::MemberAccessExpression*>(static_cast<nodes::CallExpression*>(at)->m_callee);
            if (m->is_arrow() || m->is_scope()) return 0;
            Type* rt = type_of(at);
            Sym* chosen = static_cast<nodes::CallExpression*>(at)->resolved;
            Type* declared = chosen ? return_type_of(chosen) : nullptr;
            const bool gives_reference = (rt && rt->is_reference()) || (declared && declared->is_reference()) || at->expr_type.vc == nodes::ValueCategory::LValue;
            if (!gives_reference) return 0;
            object = m->m_object;
        }
        if (!object) return 0;
        if (is_temporary_value(object)) return 8;
        at = object;
    }

    return 0;
}

std::string CppEmitter::invoke(const std::string& target, const std::vector<std::string>& codes, const std::vector<int>& classes) {
    std::size_t impure = 0, reads = 0;
    for (int c : classes) { if ((c & 3) == 2) ++impure; if ((c & 3) >= 1) ++reads; }
    const bool ordered = codes.size() >= 2 && impure >= 1 && reads >= 2;
    std::string out;

    if (!ordered) {
        out = target + "(";
        for (std::size_t i = 0; i < codes.size(); ++i) { if (i) out += ", "; out += codes[i]; }
        return out + ")";
    }

    out = "([&]() -> decltype(auto) { ";
    std::string call = target + "(";

    for (std::size_t i = 0; i < codes.size(); ++i) {
        const std::string name = "walnut_a" + std::to_string(m_temp++);
        const bool by_ref = i < classes.size() && (classes[i] & 4) && !(classes[i] & 8);
        if (i) call += ", ";

        if (by_ref) {
            out += "auto&& " + name + " = " + codes[i] + "; ";
            call += "std::forward<decltype(" + name + ")>(" + name + ")";
        } else {
            out += "auto " + name + " = " + codes[i] + "; ";
            call += "std::move(" + name + ")";
        }
    }

    return out + "return " + call + "); }())";
}

std::string CppEmitter::arith(TK op, nodes::ASTNode* l, nodes::ASTNode* r, Type* result, const nodes::ASTNode* at) {
    Type* lt = value_type(type_of(l));
    Type* rt = value_type(type_of(r));
    result = value_type(result);

    if (lt && lt->is_pointer()) {
        if (op == TK::Plus)  return "walnut_rt::ptr_add(" + expr(l) + ", " + expr(r) + ")";
        if (op == TK::Minus && rt && rt->is_pointer()) return "walnut_rt::ptr_diff<" + cpp_type(result, at) + ">(" + expr(l) + ", " + expr(r) + ")";
        if (op == TK::Minus) return "walnut_rt::ptr_sub(" + expr(l) + ", " + expr(r) + ")";
    }

    if (rt && rt->is_pointer() && op == TK::Plus) return "walnut_rt::ptr_add(" + expr(r) + ", " + expr(l) + ")";

    if (op == TK::Plus && (is_string_t(result) || is_string_t(lt) || is_string_t(rt))) {
        std::string a = is_char_t(lt) ? expr(l) : convert(expr(l), lt, m_types.builtin(BK::String), at);
        std::string b = is_char_t(rt) ? expr(r) : convert(expr(r), rt, m_types.builtin(BK::String), at);
        return invoke("walnut_rt::op_add", { a, b }, { effect_class(l), effect_class(r) });
    }

    const char* helper = arith_helper(op);
    if (!helper) { unsupported(at, "this operator"); return "0"; }

    const std::vector<int> classes{ effect_class(l), effect_class(r) };

    if (op == TK::DoubleLessThan || op == TK::DoubleGreaterThan) {
        return invoke(helper, { conv(l, result, at), expr(r) }, classes);
    }

    return invoke(helper, { conv(l, result, at), conv(r, result, at) }, classes);
}

std::string CppEmitter::call_operator_symbol(Sym* op, const std::vector<nodes::ASTNode*>& operands, const nodes::ASTNode* at) {
    if (operands.empty()) return "0";
    std::vector<Type*> params = param_types_of(op);
    std::vector<std::string> codes;
    std::vector<int> classes;

    const bool member = owner_record_of(op) != nullptr;
    const std::vector<nodes::ASTNode*> args(operands.begin() + (member ? 1 : 0), operands.end());
    call_arg_parts(op, args, at, codes, classes);
    for (std::size_t i = codes.size(); i < params.size(); ++i) { codes.push_back(cpp_type(params[i], at) + "{}"); classes.push_back(0); }
    if (member) return invoke("(" + expr(operands[0]) + ")." + function_name(op), codes, classes);
    return invoke(function_name(op), codes, classes);
}

std::string CppEmitter::pass_arg(nodes::ASTNode* arg, Type* param, const nodes::ASTNode* at) {
    if (arg && arg->kind == K::BraceInitializerList) return brace_list(static_cast<nodes::BraceInitializerList*>(arg), param, at);
    if (param && param->is_reference()) {
        auto* r = static_cast<semantics::ReferenceType*>(param);
        Type* referent = m_types.strip_cv(r->referent());
        Type* from = value_type(type_of(arg));

        if (from == referent) {
            std::string code = expr(arg);
            const bool pooled = code.size() > 2 && code.compare(0, 2, "k_") == 0 && code.find_first_not_of("0123456789", 2) == std::string::npos;
            if (pooled && r->ref_qual() == semantics::RefQual::RValue) return cpp_type(referent, at) + "(" + code + ")";
            return code;
        }

        if (referent && referent->is_array() && !static_cast<semantics::ArrayType*>(referent)->extent()) return expr(arg);
        if (from && referent && from->is_record() && referent->is_record()) {
            Sym* chosen = nullptr;
            const bool derived = semantics::rank_conversion(m_types.pointer(from, semantics::CV{}), m_types.pointer(referent, semantics::CV{}), m_types) != semantics::ConversionRank::None;
            if (!derived && semantics::rank_user_defined(from, referent, m_types, true, &chosen) != semantics::ConversionRank::None && chosen) return convert(expr(arg), from, referent, at);
            return expr(arg);
        }
        return convert(expr(arg), from, referent, at);
    }
    return conv(arg, param, at);
}

std::string CppEmitter::binary(nodes::BinaryExpression* b) {
    Type* result = type_of(b);

    if (b->memberwise) {
        const std::string code = equality_helper(b->memberwise, b) + "(" + expr(b->left) + ", " + expr(b->right) + ")";
        return b->negate_result ? "(!" + code + ")" : code;
    }

    if (b->resolved) {
        const std::vector<nodes::ASTNode*> operands = b->swap_operands ? std::vector<nodes::ASTNode*>{ b->right, b->left } : std::vector<nodes::ASTNode*>{ b->left, b->right };
        if (b->negate_result) return "(!" + call_operator_symbol(b->resolved, operands, b) + ")";
        if (b->op != TK::Equal || is_record_t(type_of(b->left))) return call_operator_symbol(b->resolved, operands, b);
    }

    if (b->op == TK::Comma) return "(static_cast<void>(" + expr(b->left) + "), " + expr(b->right) + ")";
    if (b->op == TK::LogicAnd) return "(" + truthy(b->left) + " && " + truthy(b->right) + ")";
    if (b->op == TK::LogicOr)  return "(" + truthy(b->left) + " || " + truthy(b->right) + ")";

    if (walnut::is_assignment_op(b->op) && b->left && b->left->kind == K::SubscriptExpression) {
        auto* sub = static_cast<nodes::SubscriptExpression*>(b->left);
        if (!sub->resolved && is_string_t(value_type(type_of(sub->m_array)))) {
            Type* ct = m_types.builtin(BK::Char);
            std::string value = conv(b->right, ct, b);
            if (b->op != TK::Equal) {
                const char* helper = arith_helper(compound_base(b->op));
                if (!helper) { unsupported(b, "this compound assignment on a string character"); return "0"; }
                value = std::string(helper) + "(walnut_rt::char_at(" + expr(sub->m_array) + ", " + expr(sub->m_index) + "), " + value + ")";
            }
            return "walnut_rt::set_char(" + expr(sub->m_array) + ", " + expr(sub->m_index) + ", " + value + ")";
        }
    }

    if (b->op == TK::Equal) {
        Type* lt = type_of(b->left);
        Sym* ls = b->left && b->left->kind == K::Identifier ? static_cast<nodes::Identifier*>(b->left)->resolved
                : b->left && b->left->kind == K::MemberAccessExpression && !static_cast<nodes::MemberAccessExpression*>(b->left)->is_scope() ? static_cast<nodes::MemberAccessExpression*>(b->left)->resolved : nullptr;
        if (is_lazy_field(ls)) return "(" + lazy_prefix(b->left) + "lz_set_" + local_name(ls->name) + "(" + conv(b->right, value_type(lt), b) + "))";
        return "(" + expr(b->left) + " = " + conv(b->right, lt, b) + ")";
    }

    if (walnut::is_assignment_op(b->op)) {
        Type* lt = value_type(type_of(b->left));
        const TK base = compound_base(b->op);
        const char* helper = arith_helper(base);
        if (!helper) { unsupported(b, "this compound assignment"); return "0"; }
        std::string rhs = (base == TK::DoubleLessThan || base == TK::DoubleGreaterThan) ? expr(b->right) : conv(b->right, lt, b);

        if (lt && lt->is_pointer() && (base == TK::Plus || base == TK::Minus)) {
            const std::string target = expr(b->left);
            return "(" + target + " = walnut_rt::" + (base == TK::Plus ? "ptr_add(" : "ptr_sub(") + target + ", " + expr(b->right) + "))";
        }

        if (base == TK::Plus && is_string_t(lt)) {
            rhs = is_char_t(value_type(type_of(b->right))) ? expr(b->right) : rhs;
        }

        return "walnut_rt::compound(" + expr(b->left) + ", [&](auto walnut_lhs) { return " + helper + "(walnut_lhs, " + rhs + "); })";
    }

    if (walnut::is_comparison_op(b->op) || walnut::is_equality_op(b->op)) {
        Type* lt = value_type(type_of(b->left));
        Type* rt = value_type(type_of(b->right));
        Type* c = common_type(lt, rt);
        std::string op;
        switch (b->op) {
            case TK::LogicEqual:   op = "=="; break;
            case TK::NotEqual:     op = "!="; break;
            case TK::LessThan:     op = "<";  break;
            case TK::GreaterThan:  op = ">";  break;
            case TK::LessEqual:    op = "<="; break;
            case TK::GreaterEqual: op = ">="; break;
            default: break;
        }
        std::string l = expr(b->left), r = expr(b->right);
        if (lt && !lt->is_pointer() && !lt->is_null() && rt && !rt->is_pointer() && !rt->is_null()) { l = convert(l, lt, c, b); r = convert(r, rt, c, b); }
        const int lc = effect_class(b->left), rc = effect_class(b->right);
        if ((lc == 2 || rc == 2) && lc >= 1 && rc >= 1) {
            const std::string x = "walnut_a" + std::to_string(m_temp++), y = "walnut_a" + std::to_string(m_temp++);
            return "([&]() -> bool { auto " + x + " = " + l + "; auto " + y + " = " + r + "; return " + x + " " + op + " " + y + "; }())";
        }
        return "(" + l + " " + op + " " + r + ")";
    }

    return arith(b->op, b->left, b->right, result, b);
}

std::string CppEmitter::unary(nodes::UnaryExpression* u) {
    if (u->resolved) return call_operator_symbol(u->resolved, { u->operand }, u);
    Type* rt = value_type(type_of(u));

    switch (u->op) {
        case TK::Minus:           return std::string(m_checked_arith ? "walnut_rt::checked::op_neg(" : "walnut_rt::op_neg(") + conv(u->operand, rt, u) + ")";
        case TK::Plus:            return "walnut_rt::op_pos(" + conv(u->operand, rt, u) + ")";
        case TK::ExclamationMark: return "(!" + truthy(u->operand) + ")";
        case TK::DoublePlus:      return std::string(m_checked_arith ? "walnut_rt::checked::" : "walnut_rt::") + (u->is_prefix() ? "pre_inc(" : "post_inc(") + expr(u->operand) + ")";
        case TK::DoubleMinus:     return std::string(m_checked_arith ? "walnut_rt::checked::" : "walnut_rt::") + (u->is_prefix() ? "pre_dec(" : "post_dec(") + expr(u->operand) + ")";
        case TK::Asterisk:        return "(*" + expr(u->operand) + ")";
        case TK::Ampersand:       return "(&" + expr(u->operand) + ")";
        case TK::Tilde:           return "walnut_rt::op_bitnot(" + expr(u->operand) + ")";
        default:
            unsupported(u, "this unary operator");
            return "0";
    }
}

std::string CppEmitter::subscript(nodes::SubscriptExpression* s) {
    if (s->pack_element) {
        if (s->m_array && s->m_array->kind == K::MemberAccessExpression) return member_object(static_cast<nodes::MemberAccessExpression*>(s->m_array)) + local_name(s->pack_element->name);
        return symbol_ref(s->pack_element, s);
    }
    if (s->resolved) return call_operator_symbol(s->resolved, { s->m_array, s->m_index }, s);
    Type* at = value_type(type_of(s->m_array));
    if (is_string_t(at)) return "walnut_rt::char_at(" + expr(s->m_array) + ", " + expr(s->m_index) + ")";
    return "walnut_rt::index(" + expr(s->m_array) + ", " + expr(s->m_index) + ")";
}

std::string CppEmitter::member_access(nodes::MemberAccessExpression* m) {
    Sym* s = m->resolved;

    if (m->is_scope()) {
        if (!s) { fail_at(m, "unresolved scoped name"); return "0"; }
        if (s->kind == SK::Function) { unsupported(m, "taking a scoped function as a value"); return "0"; }
        return symbol_ref(s, m);
    }

    if (!s) { fail_at(m, "unresolved member '" + std::string(m->get_member()) + "'"); return "0"; }

    const bool static_field = s->kind == SK::Variable && s->decl
        && ((s->decl->kind == K::VariableDeclaration && static_cast<nodes::VariableDeclaration*>(s->decl)->get_type_info().modifiers.has(RM::Static))
         || (s->decl->kind == K::ArrayDeclaration && static_cast<nodes::ArrayDeclaration*>(s->decl)->get_array_modifiers().has(RM::Static)));

    if (static_field) {
        if (RecordInfo* r = owner_record_of(s)) return r->cname + "::" + local_name(s->name);
    }

    if (is_lazy_field(s)) return lazy_prefix(m) + "lz_" + local_name(s->name) + "()";
    if (RecordInfo* q = qualifier_record(m)) return member_object(m) + q->cname + "::" + local_name(s->name);
    return member_object(m) + local_name(s->name);
}

auto CppEmitter::qualifier_record(const nodes::MemberAccessExpression* m) -> RecordInfo* {
    if (!m->qualifier_scope || !m->qualifier_scope->owner_symbol) return nullptr;
    auto it = m_record_by_sym.find(m->qualifier_scope->owner_symbol);
    return it == m_record_by_sym.end() ? nullptr : it->second;
}

std::string CppEmitter::member_object(nodes::MemberAccessExpression* m) {
    if (!m->is_arrow()) return expr(m->m_object) + ".";
    std::string code = expr(m->m_object);
    for (Sym* op : m->arrow_ops) code = "(" + code + ")." + function_name(op) + "()";
    return "walnut_rt::safety::checked(" + code + ")->";
}

bool CppEmitter::is_default_gap(const nodes::ASTNode* n) {
    return n && n->kind == K::Literal && static_cast<const nodes::Literal*>(n)->token_kind == TK::DefaultKeyword;
}

void CppEmitter::call_arg_parts(Sym* fn, const std::vector<nodes::ASTNode*>& args, const nodes::ASTNode* at, std::vector<std::string>& codes, std::vector<int>& classes) {
    bool has_group = false;
    std::vector<ParamSlot> slots = param_slots(fn, has_group);
    const bool skips_default = args.size() < slots.size() && std::any_of(slots.begin(), slots.end(), [](const ParamSlot& s) { return s.param && s.param->has_initializer(); });
    if (has_group || skips_default) { grouped_arg_parts(slots, args, at, codes, classes); return; }
    const nodes::FunctionParameters* ps = params_of(fn ? fn->decl : nullptr);
    std::vector<Type*> params = param_types_of(fn);
    const std::size_t n = std::max(args.size(), params.size());

    for (std::size_t i = 0; i < n; ++i) {
        if (i < args.size() && is_default_gap(args[i]) && ps && i < ps->m_params.size() && ps->m_params[i]->has_initializer()) {
            nodes::ASTNode* d = const_cast<nodes::ASTNode*>(ps->m_params[i]->get_initializer());
            codes.push_back(conv(d, params[i], at));
            classes.push_back(effect_class(d));
        } else if (i < args.size()) {
            codes.push_back(i < params.size() ? pass_arg(args[i], params[i], at) : expr(args[i]));
            classes.push_back(effect_class(args[i]) | ((i < params.size() && params[i] && params[i]->is_reference()) ? 4 : 0) | borrow_bit(args[i]));
        } else if (ps && i < ps->m_params.size() && ps->m_params[i]->has_initializer()) {
            nodes::ASTNode* d = const_cast<nodes::ASTNode*>(ps->m_params[i]->get_initializer());
            codes.push_back(conv(d, params[i], at));
            classes.push_back(effect_class(d));
        }
    }
}

void CppEmitter::grouped_arg_parts(const std::vector<ParamSlot>& slots, const std::vector<nodes::ASTNode*>& args, const nodes::ASTNode* at, std::vector<std::string>& codes, std::vector<int>& classes) {
    std::vector<semantics::ParamShape> shapes;
    std::vector<Type*> arg_types;
    for (nodes::ASTNode* a : args) arg_types.push_back(type_of(a));

    for (const ParamSlot& s : slots) {
        semantics::ParamShape sh{};
        sh.element = m_types.strip_cv(s.type);
        if (sh.element && sh.element->is_reference()) sh.element = s.type;
        sh.is_pack = s.group;
        sh.unbounded = s.group && s.param->is_infinite_variadic();
        sh.cap = s.group && !sh.unbounded ? s.param->variadic_length() : 0;
        sh.exact = s.group && s.param->is_exact_variadic() && !s.param->has_initializer();
        sh.has_default = s.param && !s.group && s.param->has_initializer();
        shapes.push_back(sh);
    }

    std::vector<std::size_t> takes;
    std::vector<Type*> spreads;
    bool any_spread = false;
    for (nodes::ASTNode* a : args) {
        Type* el = nullptr;
        if (a && a->kind == K::UnaryExpression && static_cast<nodes::UnaryExpression*>(a)->op == TK::Ellipsis) {
            Type* t = type_of(static_cast<nodes::UnaryExpression*>(a)->operand);
            if (t && t->is_reference()) t = static_cast<semantics::ReferenceType*>(t)->referent();
            t = t ? m_types.strip_cv(t) : nullptr;
            if (t && t->is_array()) el = static_cast<semantics::ArrayType*>(t)->element();
        }
        spreads.push_back(el);
        any_spread = any_spread || el;
    }
    auto saved_spread = m_types.spread_arg_element;
    if (any_spread) m_types.spread_arg_element = [&spreads](std::size_t i, std::size_t n) -> Type* { return n == spreads.size() && i < n ? spreads[i] : nullptr; };
    else m_types.spread_arg_element = nullptr;
    const bool assigned = semantics::assign_arguments(shapes, arg_types, m_types, takes);
    m_types.spread_arg_element = saved_spread;

    if (!assigned) {
        takes.assign(slots.size(), 0);
        std::size_t remaining = args.size();

        for (std::size_t i = 0; i < slots.size(); ++i) {
            std::size_t later = 0;
            for (std::size_t j = i + 1; j < slots.size(); ++j) if (!slots[j].group && !shapes[j].has_default) ++later;
            const std::size_t avail = remaining > later ? remaining - later : 0;
            takes[i] = slots[i].group ? (shapes[i].unbounded ? avail : std::min(avail, shapes[i].cap)) : std::min<std::size_t>(remaining, 1);
            remaining -= takes[i];
        }
    }

    std::size_t a = 0;

    for (std::size_t i = 0; i < slots.size(); ++i) {
        const ParamSlot& s = slots[i];
        const std::size_t took = i < takes.size() ? takes[i] : 0;

        if (s.group) {
            Type* el = m_types.strip_cv(s.type);
            bool gathered = false;
            for (std::size_t j = a; j < a + took && j < args.size(); ++j) gathered = gathered || spreads[j];
            const std::string array_type = cpp_type(m_types.array(el, std::nullopt), at);
            std::string code = gathered ? "walnut_rt::gather<" + array_type + ">(" : array_type + "{";
            int cls = 0;
            std::size_t k = 0;
            for (; k < took && a < args.size(); ++k, ++a) {
                if (k) code += ", ";
                if (spreads[a]) code += "walnut_rt::spread(" + expr(static_cast<nodes::UnaryExpression*>(args[a])->operand) + ")";
                else code += conv(args[a], el, at);
                cls = std::max(cls, effect_class(args[a]));
            }
            if (gathered) {
                codes.push_back(code + ")");
                classes.push_back(cls);
                continue;
            }
            if (s.param && s.param->has_initializer() && !s.param->is_infinite_variadic()) {
                nodes::ASTNode* d = const_cast<nodes::ASTNode*>(s.param->get_initializer());
                for (; k < s.param->variadic_length(); ++k) {
                    if (k) code += ", ";
                    code += conv(d, el, at);
                    cls = std::max(cls, effect_class(d));
                }
            }
            codes.push_back(code + "}");
            classes.push_back(cls);
            continue;
        }

        if (took && a < args.size() && is_default_gap(args[a]) && s.param && s.param->has_initializer()) {
            nodes::ASTNode* d = const_cast<nodes::ASTNode*>(s.param->get_initializer());
            codes.push_back(conv(d, s.type, at));
            classes.push_back(effect_class(d));
            ++a;
        } else if (took && a < args.size()) {
            codes.push_back(pass_arg(args[a], s.type, at));
            classes.push_back(effect_class(args[a]) | ((s.type && s.type->is_reference()) ? 4 : 0) | borrow_bit(args[a]));
            ++a;
        } else if (s.param && s.param->has_initializer()) {
            nodes::ASTNode* d = const_cast<nodes::ASTNode*>(s.param->get_initializer());
            codes.push_back(conv(d, s.type, at));
            classes.push_back(effect_class(d));
        }
    }
}

std::string CppEmitter::default_code(nodes::ASTNode* def, Type* t, const nodes::ASTNode* at) {
    semantics::ConstValue v = m_eval.eval(def);

    if (v.ok()) {
        semantics::ConstValue cv = m_eval.coerce(v, value_type(t));
        return constant_value_code(cv.ok() ? cv : v, t, at);
    }

    return conv(def, t, at);
}

void CppEmitter::function_value_args(const semantics::FunctionType* ft, const std::vector<nodes::ASTNode*>& args, const nodes::ASTNode* at, std::vector<std::string>& codes, std::vector<int>& classes) {
    const std::vector<semantics::ParamShape> shapes = semantics::shapes_of_function_type(ft);
    const std::vector<semantics::FnParamInfo>& info = ft->param_info();
    std::vector<Type*> arg_types;
    for (nodes::ASTNode* a : args) arg_types.push_back(type_of(a));
    std::vector<std::size_t> takes;
    if (!semantics::assign_arguments(shapes, arg_types, m_types, takes)) takes.assign(shapes.size(), 1);
    std::size_t a = 0;

    for (std::size_t i = 0; i < shapes.size(); ++i) {
        Type* pt = ft->params()[i];
        const std::size_t took = i < takes.size() ? takes[i] : 0;
        nodes::ASTNode* def = i < info.size() ? info[i].def : nullptr;

        if (shapes[i].is_pack) {
            Type* el = shapes[i].element;
            std::string code = cpp_type(pt, at) + "{";
            int cls = 0;
            std::size_t k = 0;

            for (; k < took && a < args.size(); ++k, ++a) {
                if (k) code += ", ";
                code += conv(args[a], el, at);
                cls = std::max(cls, effect_class(args[a]));
            }

            if (def && !shapes[i].unbounded) {
                for (; k < shapes[i].cap; ++k) {
                    if (k) code += ", ";
                    code += default_code(def, el, at);
                }
            }

            codes.push_back(code + "}");
            classes.push_back(cls);
            continue;
        }

        if (took && a < args.size()) {
            codes.push_back(pass_arg(args[a], pt, at));
            classes.push_back(effect_class(args[a]) | ((pt && pt->is_reference()) ? 4 : 0) | borrow_bit(args[a]));
            ++a;
        } else if (def) {
            codes.push_back(default_code(def, pt, at));
            classes.push_back(0);
        }
    }
}

std::string CppEmitter::call_args(Sym* fn, const std::vector<nodes::ASTNode*>& args, const nodes::ASTNode* at) {
    std::vector<std::string> codes;
    std::vector<int> classes;
    call_arg_parts(fn, args, at, codes, classes);
    std::string out;
    for (std::size_t i = 0; i < codes.size(); ++i) { if (i) out += ", "; out += codes[i]; }
    return out;
}

std::string CppEmitter::invoke_with_args(const std::string& target, Sym* fn, const std::vector<nodes::ASTNode*>& args, const nodes::ASTNode* at) {
    std::vector<std::string> codes;
    std::vector<int> classes;
    call_arg_parts(fn, args, at, codes, classes);
    return invoke(target, codes, classes);
}

std::string CppEmitter::aggregate_args(Type* t, const std::vector<nodes::ASTNode*>& els, const nodes::ASTNode* at) {
    RecordInfo* r = record_info_of(t);
    std::vector<Type*> fields;

    if (r) {
        EnvScope env(m_types, r->env ? &r->env->env : nullptr);
        for (const auto& member : r->decl->get_members()) {
            if (!member.node || member.node->kind != K::VariableDeclaration) continue;
            auto* v = static_cast<nodes::VariableDeclaration*>(member.node);
            if (v->get_type_info().modifiers.has(RM::Static) || is_lazy_decl(v)) continue;
            fields.push_back(m_types.canonicalize(v->get_type_info()));
        }
    }

    std::string out;
    for (std::size_t i = 0; i < els.size(); ++i) {
        if (i) out += ", ";
        out += i < fields.size() ? conv(els[i], fields[i], at) : expr(els[i]);
    }
    return out;
}

std::string CppEmitter::builtin_method_call(nodes::CallExpression* c) {
    auto* m = static_cast<nodes::MemberAccessExpression*>(c->m_callee);
    const std::string_view name = c->builtin_method;
    std::string obj = expr(m->m_object);
    if (m->is_arrow()) obj = "(*" + obj + ")";

    if (name == "sizeof[]") return "walnut_rt::bm::byte_size(" + obj + ", " + std::to_string(c->builtin_size) + "LL)";
    if (c->builtin_size >= 0) return "walnut_rt::i32(" + std::to_string(c->builtin_size) + "LL)";
    if (name == "sizeof") return "walnut_rt::bm::byte_size(" + obj + ")";
    if (name == "to_string" || name == "to_text") return "walnut_rt::to_display(" + obj + ")";

    std::string out = "walnut_rt::bm::" + std::string(name) + "(" + obj;
    for (nodes::ASTNode* a : c->m_arguments) out += ", " + expr(a);
    return out + ")";
}

std::string CppEmitter::call(nodes::CallExpression* c) {
    if (!c->builtin_method.empty()) return builtin_method_call(c);
    Sym* fn = c->resolved;

    if (fn && fn->decl && fn->decl->kind == K::ConstructorDeclaration) {
        return invoke_with_args(cpp_type(type_of(c), c), fn, c->m_arguments, c);
    }

    if (!fn) {
        Type* t = value_type(type_of(c));
        Sym* tcs = c->m_callee->kind == K::Identifier ? static_cast<nodes::Identifier*>(c->m_callee)->resolved : nullptr;

        if (t && !t->is_record() && tcs && tcs->kind == SK::TemplateParam) {
            if (c->m_arguments.empty()) return cpp_type(t, c) + "{}";
            return convert(expr(c->m_arguments[0]), type_of(c->m_arguments[0]), t, c, true);
        }

        if (t && t->is_record() && (c->m_callee->kind == K::Identifier || c->m_callee->kind == K::QualifiedIdentifier || c->m_callee->kind == K::TemplateInstantiation)) {
            Sym* cs = nullptr;
            if (c->m_callee->kind == K::Identifier) cs = static_cast<nodes::Identifier*>(c->m_callee)->resolved;
            if (c->m_callee->kind == K::QualifiedIdentifier) cs = static_cast<nodes::QualifiedIdentifier*>(c->m_callee)->resolved;
            if (!cs || cs->kind == SK::Type || cs->kind == SK::TemplateParam || cs->kind == SK::TypeAlias || c->m_callee->kind == K::TemplateInstantiation) {
                std::vector<std::string> codes;
                std::vector<int> classes;
                for (nodes::ASTNode* a : c->m_arguments) { codes.push_back(conv(a, t, c)); classes.push_back(effect_class(a)); }
                return invoke(cpp_type(t, c), codes, classes);
            }
        }

        Type* ct = value_type(type_of(c->m_callee));

        if (ct && ct->kind() == semantics::TypeKind::Closure && c->closure_spec) {
            auto* spec = static_cast<nodes::LambdaExpression*>(c->closure_spec);
            std::vector<std::string> codes;
            std::vector<int> classes;
            const auto* ps = spec->get_parameters();
            for (std::size_t i = 0; i < c->m_arguments.size(); ++i) {
                Type* pt = (ps && i < ps->m_params.size()) ? m_types.canonicalize(ps->m_params[i]->get_type()) : nullptr;
                codes.push_back(pt ? pass_arg(c->m_arguments[i], pt, c) : expr(c->m_arguments[i]));
                classes.push_back(effect_class(c->m_arguments[i]) | ((pt && pt->is_reference()) ? 4 : 0) | borrow_bit(c->m_arguments[i]));
            }
            return invoke("(" + expr(c->m_callee) + ")", codes, classes);
        }

        if (ct && is_dynamic(ct)) {
            std::string out = "(" + expr(c->m_callee) + ").call({";
            for (std::size_t i = 0; i < c->m_arguments.size(); ++i) { if (i) out += ", "; out += conv(c->m_arguments[i], m_types.dynamic_(), c); }
            return out + "})";
        }

        if (ct && ct->kind() == semantics::TypeKind::Function && static_cast<semantics::FunctionType*>(ct)->has_param_info()) {
            std::vector<std::string> codes;
            std::vector<int> classes;
            function_value_args(static_cast<semantics::FunctionType*>(ct), c->m_arguments, c, codes, classes);
            return invoke("(" + expr(c->m_callee) + ")", codes, classes);
        }

        if (ct && (ct->kind() == semantics::TypeKind::Function || is_record_t(ct))) {
            const std::vector<Type*>* pts = ct->kind() == semantics::TypeKind::Function ? &static_cast<semantics::FunctionType*>(ct)->params() : nullptr;
            std::string out = "(" + expr(c->m_callee) + ")(";
            for (std::size_t i = 0; i < c->m_arguments.size(); ++i) {
                if (i) out += ", ";
                Type* pt = (pts && i < pts->size()) ? (*pts)[i] : nullptr;
                out += (pt && !pt->is_dependent() && !is_dynamic(pt)) ? pass_arg(c->m_arguments[i], pt, c) : expr(c->m_arguments[i]);
            }
            return out + ")";
        }

        fail_at(c, "call target could not be resolved for code generation");
        return "0";
    }

    const bool lowered_call = fn->decl && fn->decl->kind == K::FunctionDeclaration && static_cast<nodes::FunctionDeclaration*>(fn->decl)->m_lowered_operator == nodes::OverloadableOperator::Call;

    if (fn->decl && (fn->decl->kind == K::OperatorFunctionDeclaration || lowered_call)) {
        std::vector<nodes::ASTNode*> ops;
        ops.push_back(c->m_callee);
        for (nodes::ASTNode* a : c->m_arguments) ops.push_back(a);
        return call_operator_symbol(fn, ops, c);
    }

    RecordInfo* owner = owner_record_of(fn);
    std::string target;

    if (owner) {
        const bool is_static = fn->decl && fn->decl->kind == K::FunctionDeclaration && static_cast<nodes::FunctionDeclaration*>(fn->decl)->get_modifiers().has(RM::Static);

        std::string_view qualifier;
        if (c->m_callee->kind == K::QualifiedIdentifier) {
            const auto& parts = static_cast<nodes::QualifiedIdentifier*>(c->m_callee)->parts();
            if (parts.size() >= 2) qualifier = parts[parts.size() - 2];
        } else if (c->m_callee->kind == K::MemberAccessExpression) {
            auto* m = static_cast<nodes::MemberAccessExpression*>(c->m_callee);
            if (m->is_scope() && m->m_object && m->m_object->kind == K::Identifier) qualifier = static_cast<nodes::Identifier*>(m->m_object)->get_name();
        }
        RecordInfo* via = is_static ? nullptr : named_direct_base(m_current_record, qualifier);
        const std::string scope_name = via ? via->cname : owner->cname;

        if (c->m_callee->kind == K::MemberAccessExpression) {
            auto* m = static_cast<nodes::MemberAccessExpression*>(c->m_callee);
            if (m->is_scope() || is_static) target = scope_name + "::" + function_name(fn);
            else if (RecordInfo* q = qualifier_record(m)) target = member_object(m) + q->cname + "::" + function_name(fn);
            else target = member_object(m) + function_name(fn);
        } else if (is_static || c->m_callee->kind == K::QualifiedIdentifier) {
            target = scope_name + "::" + function_name(fn);
        } else {
            target = "this->" + function_name(fn);
        }
    } else {
        target = function_name(fn);
    }

    return invoke_with_args(target, fn, c->m_arguments, c);
}

bool CppEmitter::is_generic_lambda(const nodes::LambdaExpression* l) {
    if (l->generic_origin || !l->get_parameters()) return false;
    for (const nodes::FunctionParameter* p : l->get_parameters()->m_params) {
        const auto& ti = p->get_type();
        if (ti.type && ti.type->is_primitive() && static_cast<parser_types::PrimitiveType*>(ti.type)->base_kind() == BK::Auto) return true;
    }
    return false;
}

std::string CppEmitter::lambda(nodes::LambdaExpression* l) {
    if (is_generic_lambda(l)) {
        std::string parts;
        for (std::size_t i = 0; i < l->specializations.size(); ++i) {
            if (i < l->spec_failed.size() && l->spec_failed[i]) continue;
            if (!parts.empty()) parts += ", ";
            parts += lambda(l->specializations[i]);
        }
        return parts.empty() ? "walnut_rt::overloaded<>{}" : "walnut_rt::overloaded{ " + parts + " }";
    }

    std::string caps;

    if (l->m_captures) {
        for (const auto& cap : l->m_captures->m_captures) {
            using M = nodes::LambdaCaptureItem::Mode;
            if ((cap.mode == M::ByValue || cap.mode == M::ByReference) && cap.resolved && is_global_scope_owner(cap.resolved)) continue;
            if (!caps.empty()) caps += ", ";
            switch (cap.mode) {
                case M::ByValue:         caps += local_name(cap.name); break;
                case M::ByReference:     caps += "&" + local_name(cap.name); break;
                case M::AllByValue:      caps += "="; break;
                case M::AllByReference:  caps += "&"; break;
                case M::This:            caps += "this"; break;
                case M::ThisByReference: caps += "this"; break;
                case M::InitByValue:     caps += local_name(cap.name) + " = " + expr(cap.init); break;
            }
        }
    }

    std::string out = "[" + caps + "](";

    if (auto* ps = l->get_parameters()) out += param_list(ps, l);

    Type* fnt = value_type(type_of(l));
    Type* ret = (fnt && fnt->kind() == semantics::TypeKind::Function) ? static_cast<semantics::FunctionType*>(fnt)->ret() : nullptr;
    out += ")";
    if (l->get_qualifiers().has(FQ::Mutable)) out += " mutable";
    if (ret) out += " -> " + cpp_type(ret, l);

    Type* saved = m_current_return;
    Type* saved_yield = m_yield_type;
    m_current_return = ret;
    m_yield_type = nullptr;
    const int saved_indent = m_indent;
    m_indent = 0;
    std::vector<LoopLabel> saved_loops;
    saved_loops.swap(m_loops);
    LoopLabel* saved_next_loop = m_next_loop;
    m_next_loop = nullptr;
    const bool async = l->get_qualifiers().has(FQ::Async);
    Type* body_ret = ret;
    if (async && value_type(ret) && value_type(ret)->kind() == semantics::TypeKind::Coroutine) body_ret = static_cast<semantics::CoroutineType*>(value_type(ret))->value();

    if (l->get_body() && l->get_body()->kind == K::BlockStatement) {
        std::ostringstream body;
        callable_body(body, const_cast<nodes::ASTNode*>(l->get_body()), body_ret, async, "=", false);
        out += " " + body.str();
    } else if (l->get_body()) {
        nodes::ASTNode* b = const_cast<nodes::ASTNode*>(l->get_body());
        out += " { return " + conv(b, ret, l) + "; }";
    }

    m_loops.swap(saved_loops);
    m_next_loop = saved_next_loop;
    m_indent = saved_indent;
    m_current_return = saved;
    m_yield_type = saved_yield;
    return out;
}

void CppEmitter::loop_body(std::ostream& os, nodes::ASTNode* body) {
    LoopLabel entry;
    entry.name = "walnut_redo_" + std::to_string(m_temp++);
    LoopLabel* origin = m_next_loop;
    m_next_loop = nullptr;

    if (origin) {
        entry.label = origin->label;
        entry.cont  = origin->cont;
        entry.brk   = origin->brk;
    }

    m_loops.push_back(entry);
    std::ostringstream tmp;
    block(tmp, body);
    LoopLabel done = m_loops.back();
    m_loops.pop_back();
    std::string text = tmp.str();
    if (done.used) text.insert(2, pad() + "    " + done.name + ":;\n");
    if (done.cont_used) text = "{ " + text + " " + done.cont + ":; }";
    if (origin) origin->brk_used = done.brk_used;
    os << text;
}

std::string_view CppEmitter::loop_label_of(const nodes::ASTNode* s) {
    if (!s) return {};
    switch (s->kind) {
        case K::WhileStatement:   return static_cast<const nodes::WhileStatement*>(s)->label;
        case K::DoWhileStatement: return static_cast<const nodes::DoWhileStatement*>(s)->label;
        case K::ForStatement:     return static_cast<const nodes::ForStatement*>(s)->label;
        case K::ForEachStatement: return static_cast<const nodes::ForEachStatement*>(s)->label;
        default:                  return {};
    }
}

CppEmitter::LoopLabel* CppEmitter::labelled_loop(std::string_view label) {
    for (auto it = m_loops.rbegin(); it != m_loops.rend(); ++it) if (it->label == label) return &*it;
    return nullptr;
}

bool CppEmitter::is_hoisted_function(const nodes::ASTNode* s) {
    return s && s->kind == K::FunctionDeclaration && static_cast<const nodes::FunctionDeclaration*>(s)->get_modifiers().has(RM::Hoisted);
}

bool CppEmitter::is_local_function(const Sym* s) {
    if (!s || s->kind != SK::Function || !s->owner) return false;
    const auto k = s->owner->kind;
    return k == semantics::Scope::Kind::Block || k == semantics::Scope::Kind::Function || k == semantics::Scope::Kind::Lambda;
}

std::string CppEmitter::param_type_list(const nodes::FunctionParameters* ps, const nodes::ASTNode* at) {
    std::string out;
    if (!ps) return out;
    auto add = [&](const std::string& piece) { if (!out.empty()) out += ", "; out += piece; };

    for (const nodes::FunctionParameter* p : ps->m_params) {
        if (p->expanded_pack) { for (Sym* es : p->pack_symbols) add(cpp_type(es->bound_type, at)); continue; }
        Type* pt = m_types.canonicalize(p->get_type());
        add(p->is_variadic() ? cpp_type(m_types.array(m_types.strip_cv(pt), std::nullopt), at) : cpp_type(pt, at));
    }

    return out;
}

void CppEmitter::nested_function(std::ostream& os, nodes::FunctionDeclaration* fd) {
    if (fd->get_modifiers().has(RM::Static) || fd->qualifiers().has_any(FQ::Virtual | FQ::Override)) { fail_at(fd, "a function declared inside another function cannot be static or virtual"); return; }
    nested_callable(os, fd, fd->symbol, fd->get_parameters(), fd->has_body() ? fd->get_body() : nullptr, fd->qualifiers().has(FQ::Async));
}

void CppEmitter::nested_operator(std::ostream& os, nodes::OperatorFunctionDeclaration* od) {
    if (od->is_conversion()) { fail_at(od, "a conversion operator can't be declared inside a function"); return; }
    nested_callable(os, od, od->symbol, od->get_parameters(), od->has_body() ? od->get_body() : nullptr, od->qualifiers().has(FQ::Async));
}

void CppEmitter::nested_callable(std::ostream& os, nodes::ASTNode* at, Sym* fs, const nodes::FunctionParameters* params, nodes::ASTNode* body, bool async) {
    if (!fs || !body) return;
    const std::string name = function_name(fs);
    Type* ret = return_type_of(fs);
    const std::string rt = async ? "walnut_rt::task<" + cpp_type(ret, at) + ">" : cpp_type(ret, at);
    os << pad() << "std::function<" << rt << "(" << param_type_list(params, at) << ")> " << name << ";\n";
    os << pad() << name << " = [&](" << param_list(params, at) << ") -> " << rt << " ";
    std::vector<LoopLabel> saved_loops;
    saved_loops.swap(m_loops);
    LoopLabel* saved_next_loop = m_next_loop;
    m_next_loop = nullptr;
    callable_body(os, body, ret, async, "=", false);
    os << ";\n";
    m_loops.swap(saved_loops);
    m_next_loop = saved_next_loop;
}

void CppEmitter::block(std::ostream& os, nodes::ASTNode* b) {
    os << "{\n";
    ++m_indent;
    if (b && b->kind == K::BlockStatement) {
        const auto& stmts = static_cast<nodes::BlockStatement*>(b)->statements;
        for (nodes::ASTNode* s : stmts) if (is_hoisted_function(s)) nested_function(os, static_cast<nodes::FunctionDeclaration*>(s));
        for (nodes::ASTNode* s : stmts) if (!is_hoisted_function(s)) statement(os, s);
    } else if (b) {
        statement(os, b);
    }
    --m_indent;
    os << pad() << "}";
}

void CppEmitter::field_names(RecordInfo* r, std::vector<std::string>& out, int depth) {
    if (!r || depth > 32) return;
    EnvScope env(m_types, r->env ? &r->env->env : nullptr);
    for (parser_types::TypeInfo* info : r->decl->all_bases()) field_names(record_info_of(m_types.canonicalize(*info)), out, depth + 1);

    for (const auto& member : r->decl->get_members()) {
        nodes::ASTNode* n = member.node;
        if (!n) continue;
        if (n->kind == K::VariableDeclaration && !static_cast<nodes::VariableDeclaration*>(n)->get_type_info().modifiers.has(RM::Static) && !is_lazy_decl(n)) out.push_back(local_name(static_cast<nodes::VariableDeclaration*>(n)->get_name()));
        if (n->kind == K::ArrayDeclaration && !static_cast<nodes::ArrayDeclaration*>(n)->get_array_modifiers().has(RM::Static)) out.push_back(local_name(static_cast<nodes::ArrayDeclaration*>(n)->m_name));
    }
}

std::vector<std::string> CppEmitter::binding_parts(Type* source, std::size_t count, const nodes::ASTNode* at) {
    std::vector<std::string> parts;
    source = value_type(source);
    if (source && source->is_record()) field_names(record_info_of(source), parts);
    else for (std::size_t i = 0; i < count; ++i) parts.push_back("[" + std::to_string(i) + "]");
    if (parts.size() != count) fail_at(at, "structured binding does not match its source");
    for (std::string& p : parts) if (p[0] != '[') p = "." + p;
    return parts;
}

std::string CppEmitter::binding_block(Type* source, bool by_ref, bool is_const, const std::string& init_code, const SmallVector<Sym*, 4>& symbols, const nodes::ASTNode* at) {
    const std::string hidden = "walnut_bind_" + std::to_string(m_temp++);
    std::string out;
    if (by_ref) out = std::string(is_const ? "const auto& " : "auto& ") + hidden + " = " + init_code + ";";
    else out = std::string(is_const ? "const " : "") + cpp_type(value_type(source), at) + " " + hidden + " = " + init_code + ";";
    Type* src = value_type(source);
    if (src && src->is_array() && !static_cast<semantics::ArrayType*>(src)->extent()) out += "\n" + pad() + "walnut_rt::check_binding_count(" + hidden + ", " + std::to_string(symbols.size()) + ");";
    std::vector<std::string> parts = binding_parts(source, symbols.size(), at);
    for (std::size_t i = 0; i < symbols.size() && i < parts.size(); ++i) out += "\n" + pad() + "auto& " + local_name(symbols[i]->name) + " = " + hidden + parts[i] + ";";
    return out;
}

bool CppEmitter::is_binding_symbol(const Sym* s) {
    return s && s->decl && s->decl->kind == K::VariableDeclaration && static_cast<const nodes::VariableDeclaration*>(s->decl)->is_structured_binding();
}

std::string CppEmitter::local_decl(nodes::VariableDeclaration* v, bool with_semicolon) {
    if (!v->m_bindings.empty()) {
        if (!with_semicolon) { unsupported(v, "structured bindings in this position"); return ""; }
        const bool is_const = v->get_type_info().modifiers.has(RM::Const);
        std::string init = v->binding_by_ref ? expr(v->m_initializer) : conv(v->m_initializer, v->binding_source, v);
        return binding_block(v->binding_source, v->binding_by_ref, is_const, init, v->binding_symbols, v);
    }
    Type* t = m_types.canonicalize(v->get_type_info());
    if (prim_auto(v)) t = v->get_type_info().canonical;
    std::string out = align_prefix(v->get_type_info().alignment, cpp_type(t, v));
    const auto& mods = v->get_type_info().modifiers;
    if (mods.has(RM::Static)) out += "static ";
    if (mods.has(RM::ThreadLocal)) out += "thread_local ";
    out += cpp_type(t, v) + " " + local_name(v->get_name());

    if (v->m_initializer) {
        if (v->m_initializer->kind == K::BraceInitializerList) out += " = " + brace_list(static_cast<nodes::BraceInitializerList*>(v->m_initializer), t, v);
        else out += " = " + folded_or(v, t, false);
    } else {
        out += "{}";
    }

    if (with_semicolon) out += ";";
    return out;
}

bool CppEmitter::prim_auto(nodes::VariableDeclaration* v) {
    const auto& ti = v->get_type_info();
    if (!ti.type || !ti.type->is_primitive()) return false;
    return static_cast<parser_types::PrimitiveType*>(ti.type)->base_kind() == BK::Auto;
}

std::string CppEmitter::brace_list(nodes::BraceInitializerList* list, Type* target, const nodes::ASTNode* at) {
    Type* t = value_type(target);
    Type* el = nullptr;
    if (t && t->is_array()) el = static_cast<semantics::ArrayType*>(t)->element();
    if (t && t->is_record() && !el && list->ctor && list->init_list_type) {
        Type* il = list->init_list_type;
        Type* item = nullptr;
        if (il->is_record() && static_cast<semantics::RecordType*>(il)->args().size() == 1) item = static_cast<semantics::RecordType*>(il)->args()[0].type;
        std::string inner;
        if (list->init_list_ctor) {
            inner = invoke_with_args(cpp_type(il, at), list->init_list_ctor, list->m_elements, at);
        } else {
            inner = cpp_type(il, at) + "(std::vector<" + cpp_type(item, at) + ">{";
            for (std::size_t i = 0; i < list->m_elements.size(); ++i) {
                if (i) inner += ", ";
                nodes::ASTNode* x = list->m_elements[i];
                inner += x->kind == K::BraceInitializerList ? brace_list(static_cast<nodes::BraceInitializerList*>(x), item, at) : conv(x, item, at);
            }
            inner += "})";
        }
        return cpp_type(t, at) + "(" + inner + ")";
    }
    if (t && t->is_record() && !el && list->ctor) return invoke_with_args(cpp_type(t, at), list->ctor, list->m_elements, at);
    if (t && t->is_record() && !el) return cpp_type(t, at) + "{" + aggregate_args(t, list->m_elements, at) + "}";
    if (t && !t->is_array() && !is_dynamic(t) && list->m_elements.size() == 1 && list->m_elements.front()->kind != K::BraceInitializerList) return conv(list->m_elements.front(), t, at);
    if (t && !t->is_array() && !is_dynamic(t) && list->m_elements.empty()) return cpp_type(t, at) + "{}";
    const bool fixed = t && t->is_array() && static_cast<semantics::ArrayType*>(t)->extent();
    std::string out = fixed ? "{{" : "{";
    for (std::size_t i = 0; i < list->m_elements.size(); ++i) {
        if (i) out += ", ";
        nodes::ASTNode* x = list->m_elements[i];
        if (x->kind == K::BraceInitializerList) out += brace_list(static_cast<nodes::BraceInitializerList*>(x), el, at);
        else out += el ? conv(x, el, at) : expr(x);
    }
    return out + (fixed ? "}}" : "}");
}

auto CppEmitter::array_decl_type(nodes::ArrayDeclaration* a) -> Type* {
    Type* el = m_types.canonicalize(a->get_element_type());
    std::optional<std::size_t> extent = a->m_dimension;

    if (!extent && a->m_dimension_expr) {
        semantics::ConstValue v = m_eval.eval(a->m_dimension_expr);
        if (v.is_int()) extent = static_cast<std::size_t>(v.i->get_lowest_bits());
    }

    if (!extent && a->m_initializer) extent = a->m_initializer->m_elements.size();
    return m_types.array(el, extent, semantics::CV{});
}

std::string CppEmitter::array_init(nodes::ArrayDeclaration* a, Type* t) {
    auto* at = (t && t->is_array()) ? static_cast<semantics::ArrayType*>(t) : nullptr;

    if (at && !at->extent() && a->m_dimension_expr) {
        std::string sized = "walnut_rt::sized_vector<" + cpp_type(at->element(), a) + ">(" + expr(a->m_dimension_expr) + ")";
        if (!a->m_initializer) return sized;
        return "walnut_rt::fill_from(" + sized + ", " + cpp_type(t, a) + brace_list(a->m_initializer, t, a) + ")";
    }

    if (a->m_initializer) return brace_list(a->m_initializer, t, a);
    return "{}";
}

int CppEmitter::fold_condition(nodes::IfBranch* br) {
    if (br->is_constexpr) return br->constant_value;
    if (!br->condition || br->condition->kind == K::VariableDeclaration) return -1;
    semantics::ConstValue v = m_eval.eval(br->condition);
    bool ok = false;
    const bool t = semantics::ConstEvaluator::truthy(v, ok);
    return ok ? (t ? 1 : 0) : -1;
}

void CppEmitter::if_statement(std::ostream& os, nodes::IfStatement* s) {
    bool opened = false;
    bool closed = false;

    for (nodes::IfBranch* br : s->branches) {
        const int folded = fold_condition(br);
        if (folded == 0) continue;

        if (folded == 1) {
            if (opened) os << " else ";
            else os << pad();
            block(os, br->body);
            os << "\n";
            closed = true;
            break;
        }

        if (br->condition && br->condition->kind == K::VariableDeclaration) {
            auto* v = static_cast<nodes::VariableDeclaration*>(br->condition);
            if (opened) os << " else ";
            else os << pad();
            os << "if (" << local_decl(v, false) << "; walnut_rt::truthy(" << local_name(v->get_name()) << ")) ";
        } else {
            if (opened) os << " else ";
            else os << pad();
            os << "if (" << truthy(br->condition) << ") ";
        }

        block(os, br->body);
        opened = true;
    }

    if (closed) return;

    if (s->else_branch) {
        if (opened) { os << " else "; block(os, s->else_branch); os << "\n"; }
        else { os << pad(); block(os, s->else_branch); os << "\n"; }
    } else if (opened) {
        os << "\n";
    }
}

void CppEmitter::switch_statement(std::ostream& os, nodes::SwitchStatement* s) {
    const std::string sv = "walnut_switch_" + std::to_string(m_temp++);
    const std::string ci = "walnut_case_" + std::to_string(m_temp++);
    Type* ct = value_type(type_of(s->condition));
    os << pad() << "{\n";
    ++m_indent;
    os << pad() << "const auto " << sv << " = " << expr(s->condition) << ";\n";
    os << pad() << "int " << ci << " = -1;\n";
    int default_index = -1;
    bool first = true;

    for (std::size_t i = 0; i < s->cases.size(); ++i) {
        nodes::SwitchCase* c = s->cases[i];
        if (!c->value) { default_index = static_cast<int>(i); continue; }
        Type* vt = value_type(type_of(c->value));
        Type* common = common_type(ct, vt);
        os << pad() << (first ? "if (" : "else if (") << convert(sv, ct, common, c) << " == " << convert(expr(c->value), vt, common, c) << ") " << ci << " = " << i << ";\n";
        first = false;
    }

    if (default_index >= 0) os << pad() << (first ? "" : "else ") << ci << " = " << default_index << ";\n";
    os << pad() << "switch (" << ci << ") {\n";
    ++m_indent;

    for (std::size_t i = 0; i < s->cases.size(); ++i) {
        nodes::SwitchCase* c = s->cases[i];
        os << pad() << "case " << i << ": {\n";
        ++m_indent;
        for (nodes::ASTNode* st : c->body) statement(os, st);
        --m_indent;
        os << pad() << "}\n";
    }

    --m_indent;
    os << pad() << "}\n";
    --m_indent;
    os << pad() << "}\n";
}

std::string CppEmitter::new_type_text(const nodes::NewExpression* n, const std::string& cpp) {
    std::string text;
    if (n->at) {
        const char* p = n->at;
        if (std::strncmp(p, "new", 3) == 0) p += 3;
        while (*p == ' ' || *p == '\t') ++p;
        while (*p && *p != '(' && *p != '[' && *p != '{' && *p != ';' && *p != '\n' && *p != ')' && *p != ',' && text.size() < 60) text += *p++;
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.pop_back();
    }
    if (text.empty()) {
        text = cpp;
        for (const char* prefix : { "wlu::", "walnut_rt::" }) {
            std::size_t at = 0;
            while ((at = text.find(prefix, at)) != std::string::npos) text.erase(at, std::strlen(prefix));
        }
    }
    std::string out;
    for (const char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

const nodes::ASTNode* CppEmitter::located(const nodes::ASTNode* n, int depth) {
    if (!n || depth > 16) return nullptr;
    if (n->line != 0 && n->file_id != INVALID_FILE) return n;
    const nodes::ASTNode* best = nullptr;

    nodes::for_each_child(n, [&](const nodes::ASTNode* c) {
        const nodes::ASTNode* found = located(c, depth + 1);
        if (found && (!best || found->line < best->line)) best = found;
    });

    return best;
}

void CppEmitter::line_marker(std::ostream& os, const nodes::ASTNode* n) {
    if (!source_path) return;
    n = located(n, 0);
    if (!n) return;
    std::string path = source_path(n->file_id);
    if (path.empty()) return;
    std::string escaped;
    for (char c : path) { if (c == '\\' || c == '"') escaped += '\\'; escaped += c; }
    os << "#line " << n->line << " \"" << escaped << "\"\n";
}

void CppEmitter::line_reset(std::ostream& os) {
    if (source_path) os << kLineReset << "\n";
}

void CppEmitter::statement(std::ostream& os, nodes::ASTNode* s) {
    const std::string_view label = loop_label_of(s);

    if (label.empty()) {
        statement_body(os, s);
        return;
    }

    const std::string id = std::to_string(m_temp++);
    LoopLabel names;
    names.label = label;
    names.cont  = "walnut_continue_" + id;
    names.brk   = "walnut_break_" + id;
    m_next_loop = &names;
    statement_body(os, s);
    m_next_loop = nullptr;
    if (names.brk_used) os << pad() << names.brk << ":;\n";
}

void CppEmitter::statement_body(std::ostream& os, nodes::ASTNode* s) {
    if (!s) return;
    line_marker(os, s);

    switch (s->kind) {
        case K::ExpressionStatement:
        case K::VariableDeclaration:
        case K::ArrayDeclaration:
        case K::ReturnStatement:
        case K::IfStatement:
        case K::WhileStatement:
        case K::DoWhileStatement:
        case K::ForStatement:
        case K::ForEachStatement:
        case K::SwitchStatement:
            if (source_path) {
                const nodes::ASTNode* at = located(s, 0);
                if (at && (tracks_lines ? tracks_lines(at->file_id) : !source_path(at->file_id).empty())) os << pad() << "WALNUT_HERE();\n";
            }
            break;
        default:
            break;
    }

    switch (s->kind) {
        case K::BlockStatement:
            os << pad();
            block(os, s);
            os << "\n";
            break;

        case K::ExpressionStatement:
            os << pad() << expr(static_cast<nodes::ExpressionStatement*>(s)->expr) << ";\n";
            break;

        case K::VariableDeclaration:
            if (inline_variable(s)) break;
            os << pad() << local_decl(static_cast<nodes::VariableDeclaration*>(s)) << "\n";
            break;

        case K::ArrayDeclaration: {
            auto* a = static_cast<nodes::ArrayDeclaration*>(s);
            Type* t = array_decl_type(a);
            os << pad() << align_prefix(a->m_alignment, cpp_type(t, a)) << cpp_type(t, a) << " " << local_name(a->m_name) << " = " << array_init(a, t) << ";\n";
            break;
        }

        case K::CoReturnStatement:
            os << pad() << "co_return;\n";
            break;

        case K::InvalidateStatement:
            for (nodes::ASTNode* t : static_cast<nodes::InvalidateStatement*>(s)->targets) {
                Sym* ts = t->kind == K::Identifier ? static_cast<nodes::Identifier*>(t)->resolved
                        : t->kind == K::MemberAccessExpression ? static_cast<nodes::MemberAccessExpression*>(t)->resolved : nullptr;
                if (!is_lazy_field(ts)) { fail_at(t, "'invalidate' needs a lazy field"); continue; }
                os << pad() << lazy_prefix(t) << local_name(ts->name) << ".reset(\"" << ts->name << "\");\n";
            }
            break;

        case K::ReturnStatement: {
            auto* r = static_cast<nodes::ReturnStatement*>(s);
            if (m_yield_type) { os << pad() << "co_return;\n"; break; }
            if (!r->has_value()) { os << pad() << "return;\n"; break; }
            if (m_current_return && m_current_return->is_reference()) os << pad() << "return " << expr(r->get_value()) << ";\n";
            else os << pad() << "return " << conv(r->get_value(), m_current_return, r) << ";\n";
            break;
        }

        case K::IfStatement:
            if_statement(os, static_cast<nodes::IfStatement*>(s));
            break;

        case K::WhileStatement: {
            auto* w = static_cast<nodes::WhileStatement*>(s);
            os << pad() << "while (" << truthy(w->condition) << ") ";
            loop_body(os, w->body);
            os << "\n";
            break;
        }

        case K::DoWhileStatement: {
            auto* w = static_cast<nodes::DoWhileStatement*>(s);
            os << pad() << "do ";
            loop_body(os, w->body);
            os << " while (" << truthy(w->condition) << ");\n";
            break;
        }

        case K::ForStatement: {
            auto* f = static_cast<nodes::ForStatement*>(s);
            os << pad() << "for (";
            if (f->has_var_init && f->var_init && f->var_init->kind == K::VariableDeclaration) os << local_decl(static_cast<nodes::VariableDeclaration*>(f->var_init), false);
            else if (f->initializer) os << expr(f->initializer);
            os << "; ";
            if (f->condition) os << truthy(f->condition);
            os << "; ";
            if (f->increment) os << expr(f->increment);
            os << ") ";
            loop_body(os, f->body);
            os << "\n";
            break;
        }

        case K::ForEachStatement: {
            auto* f = static_cast<nodes::ForEachStatement*>(s);

            if (f->range_sym && f->it_sym && f->end_sym) {
                Type* et = m_types.canonicalize(f->m_element_type);
                const bool by_ref = et && et->is_reference();
                const std::string tmp = "walnut_item_" + std::to_string(m_temp++);
                Type* el = value_type(type_of(f->iter_get));
                os << pad() << "{\n";
                ++m_indent;
                os << pad() << "auto&& " << local_name(f->range_sym->name) << " = " << expr(f->m_container) << ";\n";
                os << pad() << "auto " << local_name(f->it_sym->name) << " = " << expr(f->begin_call) << ";\n";
                os << pad() << "auto " << local_name(f->end_sym->name) << " = " << expr(f->end_call) << ";\n";
                os << pad() << "for (; " << truthy(f->iter_cond) << "; " << expr(f->iter_next) << ") {\n";
                ++m_indent;
                os << pad() << "auto&& " << tmp << " = " << expr(f->iter_get) << ";\n";

                if (f->is_structured_binding()) {
                    const bool is_const = f->m_modifiers.has(RM::Const) || f->m_element_type.modifiers.has(RM::Const);
                    os << pad() << binding_block(f->binding_source, f->binding_by_ref, is_const, tmp, f->binding_symbols, f) << "\n";
                } else {
                    if (!et) et = el;
                    if (by_ref) os << pad() << cpp_type(et, f) << " " << local_name(f->m_var_name) << " = " << tmp << ";\n";
                    else os << pad() << cpp_type(et, f) << " " << local_name(f->m_var_name) << " = " << (el ? convert(tmp, el, et, f) : tmp) << ";\n";
                }

                os << pad();
                loop_body(os, f->m_body);
                os << "\n";
                --m_indent;
                os << pad() << "}\n";
                --m_indent;
                os << pad() << "}\n";
                break;
            }

            Type* et = m_types.canonicalize(f->m_element_type);
            const bool by_ref = et && et->is_reference();
            const std::string tmp = "walnut_item_" + std::to_string(m_temp++);
            Type* cont = value_type(type_of(f->m_container));
            Type* el = (cont && cont->is_array()) ? static_cast<semantics::ArrayType*>(cont)->element() : nullptr;
            if (cont && cont->kind() == semantics::TypeKind::Coroutine) el = static_cast<semantics::CoroutineType*>(cont)->value();
            std::string range = f->m_container && f->m_container->kind == K::BraceInitializerList && cont && cont->is_array()
                ? cpp_type(cont, f) + brace_list(static_cast<nodes::BraceInitializerList*>(f->m_container), cont, f)
                : expr(f->m_container);
            if (is_string_t(cont)) { range = "walnut_rt::decode_utf8(" + range + ")"; el = m_types.builtin(BK::Char); }
            else if (is_dynamic(cont)) { range = "walnut_rt::dynamic_items(" + range + ")"; el = m_types.dynamic_(); }
            os << pad() << "for (auto&& " << tmp << " : " << range << ") {\n";
            ++m_indent;

            if (f->is_structured_binding()) {
                const bool is_const = f->m_modifiers.has(RM::Const) || f->m_element_type.modifiers.has(RM::Const);
                os << pad() << binding_block(f->binding_source, f->binding_by_ref, is_const, tmp, f->binding_symbols, f) << "\n";
                os << pad();
                loop_body(os, f->m_body);
                os << "\n";
                --m_indent;
                os << pad() << "}\n";
                break;
            }

            if (!et) et = el;
            if (by_ref) os << pad() << cpp_type(et, f) << " " << local_name(f->m_var_name) << " = " << tmp << ";\n";
            else os << pad() << cpp_type(et, f) << " " << local_name(f->m_var_name) << " = " << (el ? convert(tmp, el, et, f) : tmp) << ";\n";
            os << pad();
            loop_body(os, f->m_body);
            os << "\n";
            --m_indent;
            os << pad() << "}\n";
            break;
        }

        case K::SwitchStatement:
            switch_statement(os, static_cast<nodes::SwitchStatement*>(s));
            break;

        case K::SingleStatement: {
            using V = nodes::SingleStatement::Variant;
            auto* single = static_cast<nodes::SingleStatement*>(s);

            if (single->variant == V::Goto)  { os << pad() << "goto walnut_label_" << local_name(single->label) << ";\n"; break; }
            if (single->variant == V::Label) { os << pad() << "walnut_label_" << local_name(single->label) << ":;\n"; break; }

            if (!single->label.empty() && single->variant != V::Fallthrough) {
                LoopLabel* target = labelled_loop(single->label);
                if (!target) { fail_at(s, "no loop labelled '" + std::string(single->label) + "' around this statement"); break; }
                if (single->variant == V::Break)    { target->brk_used = true;  os << pad() << "goto " << target->brk << ";\n"; }
                if (single->variant == V::Continue) { target->cont_used = true; os << pad() << "goto " << target->cont << ";\n"; }
                if (single->variant == V::Repeat)   { target->used = true;      os << pad() << "goto " << target->name << ";\n"; }
                break;
            }

            switch (single->variant) {
                case V::Break:       os << pad() << "break;\n"; break;
                case V::Continue:    os << pad() << "continue;\n"; break;
                case V::Fallthrough: break;
                case V::Goto:
                case V::Label:       break;
                case V::Repeat:
                    if (m_loops.empty()) { fail_at(s, "'repeat' outside a loop"); break; }
                    m_loops.back().used = true;
                    os << pad() << "goto " << m_loops.back().name << ";\n";
                    break;
            }
            break;
        }

        case K::TryCatchStatement: {
            auto* t = static_cast<nodes::TryCatchStatement*>(s);
            os << pad() << "try ";
            block(os, t->try_body);

            for (nodes::TryCatchStatement* h = t; h; h = h->next_handler) {
                if (h->has_typed_catch) {
                    Type* ct = m_types.canonicalize(h->catch_type);
                    os << " catch (" << cpp_type(ct, h) << " " << local_name(h->catch_name) << ") ";
                } else {
                    os << " catch (...) ";
                }
                block(os, h->catch_body);
            }

            os << "\n";
            break;
        }

        case K::StaticAssertDeclaration:
        case K::UsingDeclaration:
        case K::TemplateDeclaration:
        case K::ConceptDeclaration:
            break;

        case K::FunctionDeclaration:
            nested_function(os, static_cast<nodes::FunctionDeclaration*>(s));
            break;

        case K::OperatorFunctionDeclaration:
            nested_operator(os, static_cast<nodes::OperatorFunctionDeclaration*>(s));
            break;

        case K::RecordDeclaration:
        case K::EnumDeclaration:
            break;

        default:
            unsupported(s, "this statement");
            break;
    }
}

auto CppEmitter::return_type_of(Sym* fn) -> Type* {
    if (!fn || !fn->decl) return nullptr;
    Inst* I = m_inst.owning(fn);
    if (I && I->sym == fn && I->type && I->type->kind() == semantics::TypeKind::Function) return static_cast<semantics::FunctionType*>(I->type)->ret();
    EnvScope env(m_types, I ? &I->env : nullptr);
    if (fn->decl->kind == K::FunctionDeclaration) return m_types.canonicalize(static_cast<nodes::FunctionDeclaration*>(fn->decl)->get_return_type());
    if (fn->decl->kind == K::OperatorFunctionDeclaration) {
        auto* op = static_cast<nodes::OperatorFunctionDeclaration*>(fn->decl);
        if (op->is_conversion()) return m_types.canonicalize(op->get_conversion_type());
        return m_types.canonicalize(op->get_return_type());
    }
    return nullptr;
}

std::string CppEmitter::native_type(Type* t, bool c_linkage) {
    if (t && t->is_reference()) return c_linkage ? "const char*" : "const std::string&";
    auto* b = as_builtin(t);
    if (!b) return "void";

    const unsigned bits = bit_width_of_rank(b->width());
    if (!c_linkage && b->base() == BK::Int && bits > 64) return builtin_cpp(b, nullptr);
    if (!c_linkage && b->base() == BK::Float && bits != 32 && bits != 64) return builtin_cpp(b, nullptr);

    switch (b->base()) {
        case BK::Int:    return std::string(b->is_unsigned() ? "std::uint" : "std::int") + std::to_string(bits) + "_t";
        case BK::Float:  return bits == 32 ? "float" : "double";
        case BK::Bool:   return "bool";
        case BK::Char:   return "char32_t";
        case BK::String: return c_linkage ? "const char*" : "std::string";
        default:         return "void";
    }
}

void CppEmitter::add_native_headers(const modifiers::RawModifiers& mods, std::uint32_t file_id, std::vector<std::string>& headers) {
    if (!mods.native_file_count()) return;
    const bool c_linkage = mods.linkage() == modifiers::RawModifiers::Linkage::C;

    for (std::uint8_t i = 0; i < mods.native_file_count(); ++i) {
        const std::string_view file = mods.native_file(i);
        std::string include;

        if (is_system_header(file)) {
            include = std::string(file);
        } else {
            if (!source_path) continue;
            const std::filesystem::path base = std::filesystem::path(source_path(FileId(file_id))).parent_path();
            std::error_code ec;
            std::filesystem::path full = std::filesystem::weakly_canonical(base / std::filesystem::path(std::string(file)), ec);
            if (ec) full = base / std::filesystem::path(std::string(file));
            const std::string path = full.generic_string();

            if (!is_native_header(classify_file(path))) {
                if (std::find(m_native_files.begin(), m_native_files.end(), path) == m_native_files.end()) m_native_files.push_back(path);
                continue;
            }

            include = "\"" + path + "\"";
        }

        const std::string line = c_linkage ? "extern \"C\" {\n#include " + include + "\n}\n" : "#include " + include + "\n";
        if (std::find(headers.begin(), headers.end(), line) == headers.end()) headers.push_back(line);
    }
}

void CppEmitter::emit_native_externs(std::ostream& decls, std::ostream& wrappers) {
    std::vector<std::string> headers;

    for (const Sym* s : m_native_externs) {
        auto* f = static_cast<nodes::FunctionDeclaration*>(s->decl);
        add_native_headers(f->get_modifiers(), f->file_id, headers);
    }

    for (const auto& r : m_records) {
        if (r->native) add_native_headers(r->decl->m_mods, r->decl->file_id, headers);
    }

    for (const std::string& h : headers) decls << h;
    if (!headers.empty()) decls << "\n";

    for (const Sym* s : m_native_externs) {
        auto* f = static_cast<nodes::FunctionDeclaration*>(s->decl);
        const bool c_linkage = f->get_modifiers().linkage() == modifiers::RawModifiers::Linkage::C;
        bool from_header = false;
        for (std::uint8_t i = 0; i < f->get_modifiers().native_file_count(); ++i) if (is_native_header(classify_file(f->get_modifiers().native_file(i)))) from_header = true;
        const nodes::FunctionParameters* ps = f->get_parameters();
        Type* ret = m_types.canonicalize(f->get_return_type());
        const bool returns_void = !ret || is_void(ret);

        std::vector<std::string> ns;
        for (const semantics::Scope* sc = s->owner; sc; sc = sc->parent) {
            if (sc->owner_symbol && sc->kind == semantics::Scope::Kind::Namespace) ns.insert(ns.begin(), std::string(sc->owner_symbol->name));
        }

        std::string native_params;
        std::string native_args;

        if (ps) {
            for (const nodes::FunctionParameter* p : ps->m_params) {
                Type* pt = m_types.canonicalize(p->get_type());
                const std::string nt = native_type(pt, c_linkage);
                if (!native_params.empty()) { native_params += ", "; native_args += ", "; }
                native_params += nt;
                const std::string arg = local_name(p->get_name());
                if (nt == "std::string" || nt == "const std::string&") native_args += arg;
                else native_args += "walnut_rt::to_native<" + nt + ">(" + arg + ")";
            }
        }

        const std::string native_ret = returns_void ? "void" : native_type(ret, c_linkage);
        const std::string native_name = std::string(s->name);
        std::string qualified = "::";

        if (from_header) {
            if (!c_linkage) for (const std::string& part : ns) qualified += part + "::";
        } else if (c_linkage) {
            decls << "extern \"C\" " << native_ret << " " << native_name << "(" << native_params << ");\n";
        } else {
            for (const std::string& part : ns) { decls << "namespace " << part << " { "; qualified += part + "::"; }
            decls << native_ret << " " << native_name << "(" << native_params << ");";
            for (std::size_t i = 0; i < ns.size(); ++i) decls << " }";
            decls << "\n";
        }

        qualified += native_name;
        const std::string call = qualified + "(" + native_args + ")";
        wrappers << "inline " << (returns_void ? std::string("void") : cpp_type(ret, f)) << " " << function_name(s) << "(" << param_list(ps, f) << ") {\n";
        if (returns_void) wrappers << "    " << call << ";\n";
        else wrappers << "    return walnut_rt::from_native<" << cpp_type(ret, f) << ">(" << call << ");\n";
        wrappers << "}\n\n";
    }

    if (!m_native_externs.empty()) decls << "\n";
}

std::string CppEmitter::param_list(const nodes::FunctionParameters* ps, const nodes::ASTNode* at, bool with_defaults) {
    std::string out;
    if (!ps) return out;
    auto add = [&](const std::string& piece) { if (!out.empty()) out += ", "; out += piece; };

    for (std::size_t i = 0; i < ps->m_params.size(); ++i) {
        const nodes::FunctionParameter* p = ps->m_params[i];

        if (p->expanded_pack) {
            for (Sym* es : p->pack_symbols) add(cpp_type(es->bound_type, at) + " " + local_name(es->name));
            continue;
        }

        Type* pt = m_types.canonicalize(p->get_type());

        if (p->is_variadic()) {
            add(cpp_type(m_types.array(m_types.strip_cv(pt), std::nullopt), at) + " " + local_name(p->get_name()));
            continue;
        }

        if (m_templated_params) {
            const auto t = std::find(m_templated_params->begin(), m_templated_params->end(), p);

            if (t != m_templated_params->end()) {
                add("WF" + std::to_string(t - m_templated_params->begin()) + "&& " + local_name(p->get_name()));
                continue;
            }
        }

        if (p->is_iterable && pt && pt->is_reference()) {
            Type* held = m_types.strip_cv(static_cast<semantics::ReferenceType*>(pt)->referent());
            if (held && held->kind() == semantics::TypeKind::Coroutine && static_cast<semantics::CoroutineType*>(held)->is_generator()) pt = held;
        }

        std::string piece = cpp_type(pt, at) + " " + local_name(p->get_name());
        if (with_defaults && p->has_initializer()) piece += " = " + conv(const_cast<nodes::ASTNode*>(p->get_initializer()), pt, at);
        add(piece);
    }

    return out;
}

auto CppEmitter::param_slots(const Sym* fn, bool& has_group) -> std::vector<ParamSlot> {
    std::vector<ParamSlot> out;
    has_group = false;
    const nodes::FunctionParameters* ps = params_of(fn ? fn->decl : nullptr);
    if (!ps) return out;
    Inst* I = m_inst.owning(const_cast<Sym*>(fn));
    EnvScope env(m_types, I ? &I->env : nullptr);

    for (nodes::FunctionParameter* p : ps->m_params) {
        if (p->expanded_pack) {
            for (Sym* es : p->pack_symbols) out.push_back(ParamSlot{ es->bound_type, nullptr, false });
            has_group = true;
            continue;
        }

        out.push_back(ParamSlot{ m_types.canonicalize(p->get_type()), p, p->is_variadic() });
        if (p->is_variadic()) has_group = true;
    }

    return out;
}

std::string CppEmitter::function_header(const FuncInfo& f, bool qualified, bool in_class) {
    EnvScope outer(m_types, outer_env(f));
    EnvScope env(m_types, f.inst ? &f.inst->env : nullptr);
    Type* ret = return_type_of(f.sym);
    std::string name = function_name(f.sym);
    const nodes::FunctionParameters* ps = params_of(f.decl);
    std::string out;
    const modifiers::FunctionQualifiers* q = nullptr;
    const modifiers::RawModifiers* mods = nullptr;

    if (f.decl->kind == K::FunctionDeclaration) {
        auto* fd = static_cast<nodes::FunctionDeclaration*>(f.decl);
        q = &fd->qualifiers();
        mods = &fd->get_modifiers();
    } else {
        auto* od = static_cast<nodes::OperatorFunctionDeclaration*>(f.decl);
        q = &od->qualifiers();
        mods = &od->get_modifiers();
    }

    const std::vector<const nodes::FunctionParameter*>& templated = callable_params(f);

    if (!templated.empty()) {
        out += "template <";
        for (std::size_t i = 0; i < templated.size(); ++i) out += (i ? ", typename WF" : "typename WF") + std::to_string(i);
        out += "> ";
    }

    if (mods->has(RM::Inline)) out += "WALNUT_FORCE_INLINE ";

    if (in_class) {
        if (mods->has(RM::Static)) out += "static ";
        if (q->has(FQ::Virtual) || q->has(FQ::Override)) out += "virtual ";
    }

    out += (decl_is_async(f.decl) ? "walnut_rt::task<" + cpp_type(ret, f.decl) + ">" : cpp_type(ret, f.decl)) + " ";
    if (qualified && f.owner) out += f.owner->cname + "::";
    const std::vector<const nodes::FunctionParameter*>* saved_templated = m_templated_params;
    m_templated_params = templated.empty() ? nullptr : &templated;
    out += name + "(" + param_list(ps, f.decl) + ")";
    m_templated_params = saved_templated;

    if (f.owner && q->has(FQ::Const) && !mods->has(RM::Static)) out += " const";
    return out;
}

void CppEmitter::emit_function_body(std::ostream& os, const FuncInfo& f) {
    EnvScope outer(m_types, outer_env(f));
    EnvScope env(m_types, f.inst ? &f.inst->env : nullptr);
    RecordInfo* saved_rec = m_current_record;
    Type* saved_ret = m_current_return;
    m_current_record = f.owner;
    m_current_return = return_type_of(f.sym);
    nodes::ASTNode* body = nullptr;
    if (f.decl->kind == K::FunctionDeclaration) body = static_cast<nodes::FunctionDeclaration*>(f.decl)->get_body();
    else body = static_cast<nodes::OperatorFunctionDeclaration*>(f.decl)->m_body;
    line_marker(os, f.decl);
    os << function_header(f, true, false) << " ";
    m_indent = 0;
    const bool async = decl_is_async(f.decl);
    if (async && f.sym == m_main_fn) fail_at(f.decl, "'main' cannot be async");
    std::string caps;

    if (async) {
        if (const nodes::FunctionParameters* ps = params_of(f.decl)) {
            for (const nodes::FunctionParameter* p : ps->m_params) {
                if (p->expanded_pack) { for (Sym* es : p->pack_symbols) caps += (caps.empty() ? "" : ", ") + local_name(es->name); continue; }
                Type* pt = m_types.canonicalize(const_cast<nodes::FunctionParameter*>(p)->get_type());
                caps += (caps.empty() ? "" : ", ") + std::string(pt && pt->is_reference() && !p->is_variadic() ? "&" : "") + local_name(p->get_name());
            }
        }
        const bool is_static = f.decl->kind == K::FunctionDeclaration && static_cast<nodes::FunctionDeclaration*>(f.decl)->get_modifiers().has(RM::Static);
        if (f.owner && !is_static) caps += (caps.empty() ? "" : ", ") + std::string("this");
    }

    callable_body(os, body, m_current_return, async, caps, f.sym == m_main_fn);
    os << "\n";
    line_reset(os);
    os << "\n";
    m_current_record = saved_rec;
    m_current_return = saved_ret;
}

bool CppEmitter::decl_is_async(const nodes::ASTNode* d) {
    if (!d) return false;
    if (d->kind == K::FunctionDeclaration) return static_cast<const nodes::FunctionDeclaration*>(d)->qualifiers().has(FQ::Async);
    if (d->kind == K::OperatorFunctionDeclaration) return static_cast<const nodes::OperatorFunctionDeclaration*>(d)->qualifiers().has(FQ::Async);
    if (d->kind == K::LambdaExpression) return static_cast<const nodes::LambdaExpression*>(d)->get_qualifiers().has(FQ::Async);
    return false;
}

semantics::CoroutineType* CppEmitter::generator_of(Type* t) {
    t = value_type(t);
    return (t && t->kind() == semantics::TypeKind::Coroutine && static_cast<semantics::CoroutineType*>(t)->is_generator()) ? static_cast<semantics::CoroutineType*>(t) : nullptr;
}

void CppEmitter::callable_body(std::ostream& os, nodes::ASTNode* body, Type* ret, bool async, const std::string& caps, bool is_main) {
    Type* saved_ret = m_current_return;
    Type* saved_yield = m_yield_type;
    auto with_default_return = [&]() {
        os << "{\n";
        ++m_indent;
        os << pad();
        block(os, body);
        os << "\n" << pad() << "return {};\n";
        --m_indent;
        os << pad() << "}";
    };
    const bool needs_default = ret && !is_void(ret) && (is_main || is_dynamic(value_type(ret)));

    if (semantics::CoroutineType* gen = generator_of(ret)) {
        m_uses_coroutines = true;
        m_yield_type = gen->value();
        m_current_return = nullptr;
        os << "{\n";
        ++m_indent;
        os << pad();
        block(os, body);
        os << "\n" << pad() << "co_return;\n";
        --m_indent;
        os << pad() << "}";
    } else if (async) {
        m_yield_type = nullptr;
        m_current_return = ret;
        const std::string rt = cpp_type(ret, body);
        os << "{\n";
        ++m_indent;
        os << pad() << "return walnut_rt::task<" << rt << ">([" << caps << "]() mutable -> " << rt << " ";
        if (needs_default) with_default_return(); else block(os, body);
        os << ");\n";
        --m_indent;
        os << pad() << "}";
    } else {
        m_yield_type = nullptr;
        m_current_return = ret;
        if (needs_default) with_default_return(); else block(os, body);
    }

    m_current_return = saved_ret;
    m_yield_type = saved_yield;
}

void CppEmitter::emit_enums() {
    std::set<const Sym*> done;

    for (nodes::EnumDeclaration* en : m_enum_decls) {
        if (!en->symbol || done.count(en->symbol)) continue;
        done.insert(en->symbol);
        m_enums << "enum class " << enum_name(en->symbol) << " : long long {";
        bool first = true;

        for (nodes::EnumValue* v : en->values) {
            if (!v->symbol) continue;
            semantics::ConstValue cv = enum_value(v);
            m_enums << (first ? " " : ", ") << local_name(v->name);
            if (cv.is_int()) m_enums << " = " << cv.i->to_string() << "LL";
            first = false;
        }

        m_enums << " };\n";
    }

    if (!done.empty()) m_enums << "\n";
}

std::string CppEmitter::native_record_type(RecordInfo* r) {
    std::string name;

    if (!r->decl->m_native_name.empty()) {
        name = std::string(r->decl->m_native_name);
    } else {
        std::vector<std::string> ns;
        for (const semantics::Scope* sc = r->sym ? r->sym->owner : nullptr; sc; sc = sc->parent) {
            if (sc->owner_symbol && sc->kind == semantics::Scope::Kind::Namespace) ns.insert(ns.begin(), std::string(sc->owner_symbol->name));
        }
        name = "::";
        for (const std::string& part : ns) name += part + "::";
        name += std::string(r->decl->get_name());
    }

    if (!r->inst) return name;
    name += "<";

    for (std::size_t i = 0; i < r->inst->args.size(); ++i) {
        const semantics::TemplateArg& a = r->inst->args[i];
        if (i) name += ", ";
        if (a.is_type && a.type) name += cpp_type(a.type, r->decl);
        else if (a.value) name += a.value->to_string();
        else fail_at(r->decl, "the C++ class '" + std::string(r->decl->get_name()) + "' can only take types and integers as template arguments");
    }

    return name + " >";
}

std::string CppEmitter::native_template_name(std::string_view name, RecordInfo* r) {
    if (name.find('<') == std::string_view::npos || !r || !r->inst || !r->inst->source) return std::string(name);
    const auto& params = r->inst->source->params();
    if (params.size() != r->inst->args.size()) return std::string(name);
    std::string out;
    std::size_t i = 0;

    while (i < name.size()) {
        const char c = name[i];
        const bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        if (!word) { out += c; ++i; continue; }
        std::size_t j = i;
        while (j < name.size() && ((name[j] >= 'a' && name[j] <= 'z') || (name[j] >= 'A' && name[j] <= 'Z') || (name[j] >= '0' && name[j] <= '9') || name[j] == '_')) ++j;
        const std::string_view ident = name.substr(i, j - i);
        bool replaced = false;

        for (std::size_t k = 0; k < params.size(); ++k) {
            if (!params[k] || params[k]->m_name != ident) continue;
            const semantics::TemplateArg& a = r->inst->args[k];
            if (a.is_type && a.type) { out += cpp_type(a.type, r->decl); replaced = true; }
            else if (a.value) { out += a.value->to_string(); replaced = true; }
            break;
        }

        if (!replaced) out += std::string(ident);
        i = j;
    }

    return out;
}

bool CppEmitter::declared_is_param(const parser_types::TypeInfo& info) {
    std::vector<const semantics::SubstEnv*> saved = m_types.suspend_subst();
    Type* t = value_type(m_types.canonicalize(info));
    m_types.resume_subst(std::move(saved));
    return t && t->kind() == semantics::TypeKind::TypeParam;
}

std::string CppEmitter::native_args(const nodes::FunctionParameters* ps) {
    std::string out;
    if (!ps) return out;

    for (const nodes::FunctionParameter* p : ps->m_params) {
        if (p->expanded_pack) {
            for (Sym* es : p->pack_symbols) { if (!out.empty()) out += ", "; out += local_name(es->name); }
            continue;
        }

        if (!out.empty()) out += ", ";
        const std::string name = local_name(p->get_name());

        if (p->is_variadic()) {
            out += name;
            continue;
        }

        Type* pt = m_types.canonicalize(p->get_type());
        Type* vt = value_type(pt);
        RecordInfo* rr = record_info_of(vt);
        auto* b = as_builtin(vt);
        const bool moved = pt && pt->is_reference() && static_cast<semantics::ReferenceType*>(pt)->ref_qual() == semantics::RefQual::RValue;

        if (rr && rr->native && !declared_is_param(p->get_type())) {
            out += moved ? "std::move(" + name + ".native)" : name + ".native";
        } else if (b && (b->base() == BK::Int || b->base() == BK::Float || b->base() == BK::Bool || b->base() == BK::Char) && !declared_is_param(p->get_type())) {
            out += "walnut_rt::to_native<" + native_type(vt, false) + ">(" + name + ")";
        } else {
            out += moved ? "std::move(" + name + ")" : name;
        }
    }

    return out;
}

std::string CppEmitter::native_body(const FuncInfo& f, const std::string& native_name) {
    const nodes::FunctionParameters* ps = params_of(f.decl);
    const std::string args = native_args(ps);
    const modifiers::RawModifiers& mods = f.decl->kind == K::FunctionDeclaration ? static_cast<nodes::FunctionDeclaration*>(f.decl)->get_modifiers() : static_cast<nodes::OperatorFunctionDeclaration*>(f.decl)->get_modifiers();
    const bool is_static = mods.has(RM::Static);
    std::string_view as = f.decl->kind == K::FunctionDeclaration ? static_cast<nodes::FunctionDeclaration*>(f.decl)->native_name : static_cast<nodes::OperatorFunctionDeclaration*>(f.decl)->native_name;
    std::string call;

    if (f.decl->kind == K::OperatorFunctionDeclaration && as.empty()) {
        auto* od = static_cast<nodes::OperatorFunctionDeclaration*>(f.decl);

        if (od->is_conversion()) {
            Type* ct = m_types.canonicalize(od->get_conversion_type());
            Type* cv = value_type(ct);
            auto* b = as_builtin(cv);
            const bool plain = b && (b->base() == BK::Int || b->base() == BK::Float || b->base() == BK::Bool || b->base() == BK::Char) && !declared_is_param(od->get_conversion_type());
            call = "static_cast<" + (plain ? native_type(cv, false) : cpp_type(ct, f.decl)) + ">(native)";
        } else {
            const std::string op = nodes::overloadable_operator_name(od->get_overload());
            const std::size_t n = ps ? ps->m_params.size() : 0;
            if (op == "[]") call = "native[" + args + "]";
            else if (op == "()") call = "native(" + args + ")";
            else if (op == "++" || op == "--") call = n == 0 ? "(" + op + "native)" : "(native" + op + ")";
            else if (n == 0) call = "(" + op + "native)";
            else call = "(native " + op + " " + args + ")";
        }
    } else {
        const std::string name = as.empty() ? std::string(f.sym->name) : native_template_name(as, f.owner);

        if (name.find("::") != std::string::npos) call = name + "(" + (is_static ? args : (args.empty() ? std::string("native") : "native, " + args)) + ")";
        else if (is_static) call = native_name + "::" + name + "(" + args + ")";
        else call = "native." + name + "(" + args + ")";
    }

    Type* ret = return_type_of(f.sym);
    if (!ret || is_void(ret)) return call + ";";
    Type* rv = value_type(ret);
    RecordInfo* rr = record_info_of(rv);
    const parser_types::TypeInfo* declared = nullptr;
    if (f.decl->kind == K::FunctionDeclaration) declared = &static_cast<nodes::FunctionDeclaration*>(f.decl)->get_return_type();
    else if (!static_cast<nodes::OperatorFunctionDeclaration*>(f.decl)->is_conversion()) declared = &static_cast<nodes::OperatorFunctionDeclaration*>(f.decl)->get_return_type();
    else declared = &static_cast<nodes::OperatorFunctionDeclaration*>(f.decl)->get_conversion_type();

    if (rr && rr->native && !declared_is_param(*declared)) {
        if (!ret->is_reference()) return "return " + rr->cname + "(walnut_rt::native_tag{}, " + call + ");";
        if (rr == f.owner && !is_static) return call + "; return *this;";
        fail_at(f.decl, "a method of a C++ class can only return a reference to its own class or to a template parameter");
        return call + ";";
    }

    if (ret->is_pointer() && !is_static) return "return walnut_rt::native_ptr<" + cpp_type(ret, f.decl) + ">(native, " + call + ");";
    auto* b = as_builtin(rv);
    if (!ret->is_reference() && b && !declared_is_param(*declared)) return "return walnut_rt::from_native<" + cpp_type(rv, f.decl) + ">(" + call + ");";
    return "return " + call + ";";
}

void CppEmitter::emit_native_record(std::ostream& os, RecordInfo* r) {
    EnvScope env(m_types, r->env ? &r->env->env : nullptr);
    RecordInfo* saved = m_current_record;
    m_current_record = r;
    const std::string nt = native_record_type(r);
    bool has_ctor = false;

    os << "struct " << r->cname << " {\n";
    os << "    " << nt << " native;\n";
    os << "    template <typename N_> " << r->cname << "(walnut_rt::native_tag, N_&& n) : native(std::forward<N_>(n)) {}\n";

    for (const auto& member : r->decl->get_members()) {
        nodes::ASTNode* n = member.node;
        if (!n) continue;

        if (n->kind == K::ConstructorDeclaration) {
            auto* c = static_cast<nodes::ConstructorDeclaration*>(n);
            has_ctor = true;
            const bool forwarded = c->m_special != nodes::ConstructorDeclaration::Special::Delete && !c->m_body;
            os << "    " << (forwarded ? "template <int W_ = 0> " : "") << (c->m_qualifiers.has(FQ::Explicit) ? "explicit " : "") << r->cname << "(" << param_list(c->get_parameters(), c, true) << ")";
            if (c->m_special == nodes::ConstructorDeclaration::Special::Delete) os << " = delete;\n";
            else if (c->m_body) os << ";\n";
            else if (!c->native_name.empty()) os << " : native(" << native_template_name(c->native_name, r) << "(" << native_args(c->get_parameters()) << ")) {}\n";
            else os << " : native(" << native_args(c->get_parameters()) << ") {}\n";
            continue;
        }

        if (n->kind != K::FunctionDeclaration && n->kind != K::OperatorFunctionDeclaration) continue;
        Sym* fs = n->kind == K::FunctionDeclaration ? static_cast<nodes::FunctionDeclaration*>(n)->symbol : static_cast<nodes::OperatorFunctionDeclaration*>(n)->symbol;
        const bool lazy = n->kind == K::FunctionDeclaration ? static_cast<nodes::FunctionDeclaration*>(n)->lazy_untyped : static_cast<nodes::OperatorFunctionDeclaration*>(n)->lazy_untyped;
        if (!fs || lazy) continue;
        if (n->kind == K::OperatorFunctionDeclaration && static_cast<nodes::OperatorFunctionDeclaration*>(n)->m_deleted) continue;
        FuncInfo f{ n, fs, r->env, r };
        const bool has_body = n->kind == K::FunctionDeclaration ? static_cast<nodes::FunctionDeclaration*>(n)->has_body() : static_cast<nodes::OperatorFunctionDeclaration*>(n)->has_body();
        const bool used = n->kind == K::FunctionDeclaration ? static_cast<nodes::FunctionDeclaration*>(n)->native_used : (static_cast<nodes::OperatorFunctionDeclaration*>(n)->native_used || static_cast<nodes::OperatorFunctionDeclaration*>(n)->is_conversion());
        if (has_body) os << "    " << function_header(f, false, true) << ";\n";
        else if (!used) continue;
        else os << "    template <int W_ = 0> " << function_header(f, false, true) << " { " << native_body(f, nt) << " }\n";
    }

    for (const FuncInfo& f : m_functions) {
        if (f.owner != r || !f.inst || f.inst->sym != f.sym || f.inst == r->env) continue;
        os << "    " << function_header(f, false, true) << ";\n";
    }

    if (!has_ctor) os << "    " << r->cname << "() = default;\n";
    os << "};\n\n";
    m_current_record = saved;
}

void CppEmitter::emit_record(std::ostream& os, RecordInfo* r) {
    if (r->native) { emit_native_record(os, r); return; }
    EnvScope env(m_types, r->env ? &r->env->env : nullptr);
    RecordInfo* saved = m_current_record;
    m_current_record = r;
    long long struct_align = r->decl->m_alignment ? r->decl->m_alignment->align_value : 0;
    bool has_field = false;

    for (const auto& member : r->decl->get_members()) {
        if (member.node && member.node->kind == K::VariableDeclaration && !static_cast<nodes::VariableDeclaration*>(member.node)->get_type_info().modifiers.has(RM::Static)) has_field = true;
        if (member.node && member.node->kind == K::ArrayDeclaration && !static_cast<nodes::ArrayDeclaration*>(member.node)->get_array_modifiers().has(RM::Static)) has_field = true;
    }

    const bool is_union = r->decl->is_union();
    bool union_init_done = false;
    bool union_has_explicit_init = false;
    if (is_union) {
        for (const auto& member : r->decl->get_members()) {
            if (!member.node) continue;
            if (member.node->kind == K::VariableDeclaration && static_cast<nodes::VariableDeclaration*>(member.node)->m_initializer && !static_cast<nodes::VariableDeclaration*>(member.node)->get_type_info().modifiers.has(RM::Static)) union_has_explicit_init = true;
            if (member.node->kind == K::ArrayDeclaration && static_cast<nodes::ArrayDeclaration*>(member.node)->m_initializer && !static_cast<nodes::ArrayDeclaration*>(member.node)->get_array_modifiers().has(RM::Static)) union_has_explicit_init = true;
        }
    }

    os << (is_union ? "union " : "struct ");
    if (struct_align > 0 && !has_field) { os << "alignas(" << struct_align << ") "; struct_align = 0; }
    os << r->cname;

    bool first_base = true;

    for (parser_types::TypeInfo* info : r->decl->all_bases()) {
        Type* base = m_types.canonicalize(*info);
        if (RecordInfo* b = record_info_of(base)) { os << (first_base ? " : public " : ", public ") << b->cname; first_base = false; }
        else fail_at(r->decl, "base class of '" + r->cname + "' has no generated definition");
    }

    os << " {\n";
    m_indent = 1;
    bool has_virtual = false;
    bool polymorphic = false;

    for (const auto& member : r->decl->get_members()) {
        nodes::ASTNode* n = member.node;
        if (!n) continue;
        if (n->kind == K::FunctionDeclaration && static_cast<nodes::FunctionDeclaration*>(n)->qualifiers().has_any(FQ::Virtual | FQ::Override)) polymorphic = true;
        if (n->kind == K::OperatorFunctionDeclaration && static_cast<nodes::OperatorFunctionDeclaration*>(n)->qualifiers().has_any(FQ::Virtual | FQ::Override)) polymorphic = true;
        if (n->kind == K::DestructorDeclaration && static_cast<nodes::DestructorDeclaration*>(n)->m_qualifiers.has(FQ::Virtual)) polymorphic = true;
    }

    for (const auto& member : r->decl->get_members()) {
        nodes::ASTNode* n = member.node;
        if (!n) continue;

        switch (n->kind) {
            case K::VariableDeclaration: {
                auto* v = static_cast<nodes::VariableDeclaration*>(n);
                if (is_lazy_decl(v)) break;
                Type* t = m_types.canonicalize(v->get_type_info());
                const bool is_static = v->get_type_info().modifiers.has(RM::Static);
                const bool is_mutable = v->get_type_info().modifiers.has(RM::Mutable);
                std::string aligned = align_prefix(v->get_type_info().alignment, cpp_type(m_types.strip_cv(t), v));
                if (!is_static && struct_align > 0) { aligned = "alignas(" + std::to_string(struct_align) + ") alignas(" + cpp_type(m_types.strip_cv(t), v) + ") " + aligned; struct_align = 0; }
                os << pad() << aligned << (is_static ? "static inline " : "") << (is_mutable ? "mutable " : "") << cpp_type(m_types.strip_cv(t), v) << " " << local_name(v->get_name());
                if (v->m_initializer) {
                    if (v->m_initializer->kind == K::BraceInitializerList) os << " = " << brace_list(static_cast<nodes::BraceInitializerList*>(v->m_initializer), t, v);
                    else os << " = " << folded_or(v, t, is_static);
                } else if (is_union && !is_static) {
                    if (!union_has_explicit_init && !union_init_done) { os << "{}"; union_init_done = true; }
                } else if (!is_record_t(t) && !(t && t->is_reference())) {
                    os << "{}";
                }
                os << ";\n";
                break;
            }

            case K::ArrayDeclaration: {
                auto* a = static_cast<nodes::ArrayDeclaration*>(n);
                Type* t = array_decl_type(a);
                const auto& amods = a->get_array_modifiers();
                std::string aligned = align_prefix(a->m_alignment, cpp_type(t, a));
                if (!amods.has(RM::Static) && struct_align > 0) { aligned = "alignas(" + std::to_string(struct_align) + ") alignas(" + cpp_type(t, a) + ") " + aligned; struct_align = 0; }
                os << pad() << aligned << (amods.has(RM::Static) ? "static inline " : "") << (amods.has(RM::Mutable) ? "mutable " : "") << cpp_type(t, a) << " " << local_name(a->m_name);
                if (is_union && !amods.has(RM::Static) && !a->m_initializer) {
                    if (!union_has_explicit_init && !union_init_done) { os << "{}"; union_init_done = true; }
                } else {
                    os << " = " << array_init(a, t);
                }
                os << ";\n";
                break;
            }

            case K::ConstructorDeclaration: {
                auto* c = static_cast<nodes::ConstructorDeclaration*>(n);
                using SP = nodes::ConstructorDeclaration::Special;
                if (c->m_special == SP::Delete && self_ref_kind(r, c->get_parameters()) == 2) break;
                os << pad() << (c->m_qualifiers.has(FQ::Explicit) ? "explicit " : "") << r->cname << "(" << param_list(c->get_parameters(), c, true) << ")";
                if (c->m_special == SP::Default) { os << " = default;\n"; break; }
                if (c->m_special == SP::Delete)  { os << " = delete;\n"; break; }
                os << ";\n";
                break;
            }

            case K::DestructorDeclaration: {
                auto* d = static_cast<nodes::DestructorDeclaration*>(n);
                using SP = nodes::DestructorDeclaration::Special;
                const bool virt = d->m_qualifiers.has(FQ::Virtual) || polymorphic;
                os << pad() << (virt ? "virtual " : "") << "~" << r->cname << "()";
                if (d->m_special == SP::Default) { os << " noexcept(false) {}\n"; break; }
                if (d->m_special == SP::Delete)  { os << " = delete;\n"; break; }
                os << " noexcept(false);\n";
                break;
            }

            case K::FunctionDeclaration:
            case K::OperatorFunctionDeclaration: {
                Sym* fs = n->kind == K::FunctionDeclaration ? static_cast<nodes::FunctionDeclaration*>(n)->symbol : static_cast<nodes::OperatorFunctionDeclaration*>(n)->symbol;
                const modifiers::RawModifiers& mods = n->kind == K::FunctionDeclaration ? static_cast<nodes::FunctionDeclaration*>(n)->get_modifiers() : static_cast<nodes::OperatorFunctionDeclaration*>(n)->get_modifiers();
                if (mods.has(RM::Friend) || !fs) break;
                if (n->kind == K::FunctionDeclaration && static_cast<nodes::FunctionDeclaration*>(n)->lazy_untyped) break;
                if (n->kind == K::OperatorFunctionDeclaration && static_cast<nodes::OperatorFunctionDeclaration*>(n)->lazy_untyped) break;
                FuncInfo f{ n, fs, r->env, r };
                const modifiers::FunctionQualifiers& q = n->kind == K::FunctionDeclaration ? static_cast<nodes::FunctionDeclaration*>(n)->qualifiers() : static_cast<nodes::OperatorFunctionDeclaration*>(n)->qualifiers();
                if (q.has(FQ::Virtual) || q.has(FQ::Override)) has_virtual = true;
                os << pad() << function_header(f, false, true);
                const bool has_body = n->kind == K::FunctionDeclaration ? static_cast<nodes::FunctionDeclaration*>(n)->has_body() : static_cast<nodes::OperatorFunctionDeclaration*>(n)->has_body();
                if (!has_body && (q.has(FQ::Virtual))) os << " = 0";
                os << ";\n";
                break;
            }

            case K::RecordDeclaration:
            case K::EnumDeclaration:
            case K::TemplateDeclaration:
            case K::StaticAssertDeclaration:
            case K::UsingDeclaration:
            case K::TypeQueryOverload:
                break;

            default:
                unsupported(n, "this record member");
                break;
        }
    }

    for (const FuncInfo& f : m_functions) {
        if (f.owner != r || !f.inst || f.inst->sym != f.sym || f.inst == r->env) continue;
        os << pad() << function_header(f, false, true) << ";\n";
    }

    for (Inst* I : m_ctor_insts) {
        if (owner_record_of(I->sym) != r) continue;
        EnvScope inst_env(m_types, &I->env);
        auto* c = static_cast<nodes::ConstructorDeclaration*>(I->decl);
        os << pad() << (ctor_instance_self_kind(I) == 2 ? "template <int W_ = 0> " : "") << (c->m_qualifiers.has(FQ::Explicit) ? "explicit " : "") << r->cname << "(" << param_list(c->get_parameters(), c, true) << ");\n";
    }

    emit_lazy_fields(os, r);
    emit_special_members(os, r, has_virtual || polymorphic);

    m_indent = 0;
    os << "};\n\n";
    m_current_record = saved;
}

void CppEmitter::emit_record_definitions(std::ostream& os, RecordInfo* r) {
    EnvScope env(m_types, r->env ? &r->env->env : nullptr);
    RecordInfo* saved = m_current_record;
    m_current_record = r;

    if (r->native) {
        for (const auto& member : r->decl->get_members()) {
            if (!member.node || member.node->kind != K::ConstructorDeclaration) continue;
            auto* c = static_cast<nodes::ConstructorDeclaration*>(member.node);
            if (!c->m_body) continue;
            line_marker(os, c);
            os << r->cname << "::" << r->cname << "(" << param_list(c->get_parameters(), c) << ") : native() ";
            Type* saved_ret = m_current_return;
            m_current_return = m_types.void_();
            m_indent = 0;
            block(os, c->m_body);
            m_current_return = saved_ret;
            os << "\n";
            line_reset(os);
            os << "\n";
        }

        m_current_record = saved;
        return;
    }

    emit_lazy_definitions(os, r);

    for (const auto& member : r->decl->get_members()) {
        nodes::ASTNode* n = member.node;
        if (!n) continue;

        if (n->kind == K::ConstructorDeclaration) {
            auto* c = static_cast<nodes::ConstructorDeclaration*>(n);
            if (c->m_special != nodes::ConstructorDeclaration::Special::None) continue;
            emit_constructor_definition(os, r, c);
        } else if (n->kind == K::DestructorDeclaration) {
            auto* d = static_cast<nodes::DestructorDeclaration*>(n);
            using SP = nodes::DestructorDeclaration::Special;
            if (d->m_special != SP::None) continue;
            line_marker(os, d);
            os << r->cname << "::~" << r->cname << "() noexcept(false) ";
            Type* saved_ret = m_current_return;
            m_current_return = m_types.void_();
            m_indent = 0;
            block(os, d->m_body);
            m_current_return = saved_ret;
            os << "\n";
            line_reset(os);
            os << "\n";
        }
    }

    for (Inst* I : m_ctor_insts) {
        if (owner_record_of(I->sym) != r) continue;
        EnvScope inst_env(m_types, &I->env);
        auto* c = static_cast<nodes::ConstructorDeclaration*>(I->decl);

        if (ctor_instance_self_kind(I) == 2) {
            os << "template <int W_>\n";
            emit_constructor_definition(os, r, c);
            os << "template " << r->cname << "::" << r->cname << "(" << param_list(c->get_parameters(), c) << ");\n\n";
        } else {
            emit_constructor_definition(os, r, c);
        }
    }

    m_current_record = saved;
}

int CppEmitter::ctor_instance_self_kind(Inst* I) {
    if (!I || !I->sym) return 0;
    RecordInfo* r = owner_record_of(I->sym);
    if (!r || !r->sym) return 0;
    if (!I->type || I->type->kind() != semantics::TypeKind::Function) return 0;
    const auto& ps = static_cast<semantics::FunctionType*>(I->type)->params();
    if (ps.size() != 1 || !ps[0]) return 0;
    Type* p = ps[0];
    Type* pv = p->is_reference() ? static_cast<semantics::ReferenceType*>(p)->referent() : p;
    if (record_info_of(pv) != r) return 0;
    if (!p->is_reference()) return 1;
    auto* rt = static_cast<semantics::ReferenceType*>(p);
    if (rt->ref_qual() == semantics::RefQual::LValue && pv->cv().is_const && !pv->cv().is_volatile) return 1;
    if (rt->ref_qual() == semantics::RefQual::RValue && pv->cv().bits() == 0) return 1;
    return 2;
}

void CppEmitter::emit_constructor_definition(std::ostream& os, RecordInfo* r, nodes::ConstructorDeclaration* c) {
    line_marker(os, c);
    os << r->cname << "::" << r->cname << "(" << param_list(c->get_parameters(), c) << ")";
    std::vector<std::string> inits;

    for (nodes::ASTNode* init : c->get_init_list()) {
        if (init && init->kind == K::BraceConstructExpression) {
            auto* bc = static_cast<nodes::BraceConstructExpression*>(init);
            Sym* target = bc->m_callee && bc->m_callee->kind == K::Identifier ? static_cast<nodes::Identifier*>(bc->m_callee)->resolved : nullptr;
            std::vector<nodes::ASTNode*> els = bc->m_init ? bc->m_init->m_elements : std::vector<nodes::ASTNode*>{};

            if (target && target->kind == SK::Variable) {
                Type* ft = target->decl && target->decl->kind == K::ArrayDeclaration
                    ? array_decl_type(static_cast<nodes::ArrayDeclaration*>(target->decl))
                    : (target->type ? m_types.canonicalize(*target->type) : nullptr);
                Type* fv = ft ? m_types.strip_cv(value_type(ft)) : nullptr;

                if (bc->ctor) inits.push_back(local_name(target->name) + "(" + call_args(bc->ctor, els, init) + ")");
                else if (fv && fv->is_record()) inits.push_back(local_name(target->name) + "{" + aggregate_args(fv, els, init) + "}");
                else if (fv && fv->is_array() && bc->m_init) inits.push_back(local_name(target->name) + "(" + brace_list(bc->m_init, ft, init) + ")");
                else if (els.size() == 1) inits.push_back(local_name(target->name) + "(" + conv(els[0], ft, init) + ")");
                else if (els.empty()) inits.push_back(local_name(target->name) + "{}");
                else unsupported(init, "this brace initializer for a non-array field");
            } else if (target && target->kind == SK::Type) {
                RecordInfo* b = init_record(r, target);
                if (!b) { unsupported(init, "this base initializer"); continue; }
                inits.push_back(b->cname + "(" + (bc->ctor ? call_args(bc->ctor, els, init) : std::string()) + ")");
            } else {
                unsupported(init, "this constructor initializer");
            }

            continue;
        }

        if (!init || init->kind != K::CallExpression) { unsupported(init, "this constructor initializer"); continue; }
        auto* call_e = static_cast<nodes::CallExpression*>(init);
        Sym* target = nullptr;
        if (call_e->m_callee->kind == K::Identifier) target = static_cast<nodes::Identifier*>(call_e->m_callee)->resolved;

        if (target && target->kind == SK::Variable && call_e->resolved && call_e->resolved->decl && call_e->resolved->decl->kind == K::ConstructorDeclaration) {
            inits.push_back(local_name(target->name) + "(" + call_args(call_e->resolved, call_e->m_arguments, init) + ")");
        } else if (target && target->kind == SK::Variable) {
            Type* ft = target->type ? m_types.canonicalize(*target->type) : nullptr;
            std::string args;
            for (std::size_t i = 0; i < call_e->m_arguments.size(); ++i) {
                if (i) args += ", ";
                args += (call_e->m_arguments.size() == 1 && ft) ? conv(call_e->m_arguments[i], ft, init) : expr(call_e->m_arguments[i]);
            }
            inits.push_back(local_name(target->name) + "(" + args + ")");
        } else if (target && target->kind == SK::Type) {
            RecordInfo* b = init_record(r, target);
            if (!b) { unsupported(init, "this base initializer"); continue; }
            inits.push_back(b->cname + "(" + call_args(call_e->resolved, call_e->m_arguments, init) + ")");
        } else {
            unsupported(init, "this constructor initializer");
        }
    }

    if (!inits.empty()) {
        os << " : ";
        for (std::size_t i = 0; i < inits.size(); ++i) { if (i) os << ", "; os << inits[i]; }
    }

    os << " ";
    Type* saved_ret = m_current_return;
    m_current_return = m_types.void_();
    m_indent = 0;
    block(os, c->m_body);
    m_current_return = saved_ret;
    os << "\n";
    line_reset(os);
    os << "\n";
}

void CppEmitter::emit_prototypes(std::ostream& os) {
    for (const FuncInfo& f : m_functions) {
        if (f.owner) continue;
        os << function_header(f, false, false) << ";\n";
    }
}

auto CppEmitter::declared_type(nodes::VariableDeclaration* v) -> Type* {
    if (prim_auto(v)) return v->get_type_info().canonical;
    return m_types.canonicalize(v->get_type_info());
}

bool CppEmitter::is_closure_global(const Sym* s) {
    if (!s || !s->decl || s->decl->kind != K::VariableDeclaration || !is_global_scope_owner(s)) return false;
    Type* t = value_type(declared_type(static_cast<nodes::VariableDeclaration*>(s->decl)));
    return t && t->kind() == semantics::TypeKind::Closure;
}

bool CppEmitter::is_reference_global(const Sym* s) {
    if (!s || !s->decl || s->decl->kind != K::VariableDeclaration) return false;
    Type* t = declared_type(static_cast<nodes::VariableDeclaration*>(s->decl));
    return t && t->is_reference();
}

std::string CppEmitter::direct_init_args(nodes::ASTNode* init, Type* t, const nodes::ASTNode* at) {
    Type* want = value_type(t);

    if (init->kind == K::CallExpression) {
        auto* c = static_cast<nodes::CallExpression*>(init);
        if (c->resolved && c->resolved->decl && c->resolved->decl->kind == K::ConstructorDeclaration && value_type(type_of(c)) == want) return call_args(c->resolved, c->m_arguments, at);
    }

    if (init->kind == K::BraceConstructExpression) {
        auto* b = static_cast<nodes::BraceConstructExpression*>(init);
        if (b->ctor && value_type(type_of(b)) == want) return call_args(b->ctor, b->m_init ? b->m_init->m_elements : std::vector<nodes::ASTNode*>{}, at);
    }

    if (init->kind == K::BraceInitializerList) return brace_list(static_cast<nodes::BraceInitializerList*>(init), t, at);
    return conv(init, t, at);
}

std::string CppEmitter::binding_global(const nodes::VariableDeclaration* v) {
    auto it = m_binding_globals.find(v);
    if (it != m_binding_globals.end()) return it->second;
    std::string name = unique_global("g_bind");
    m_binding_globals[v] = name;
    return name;
}

void CppEmitter::emit_globals(std::ostream& os, bool as_extern) {
    if (as_extern) {
        std::ostringstream full;
        emit_globals(full, false);
        std::istringstream lines(full.str());
        std::string line;

        while (std::getline(lines, line)) {
            if (line.empty()) continue;
            if (line.rfind("inline ", 0) == 0) { os << line << "\n"; continue; }

            while (line.rfind("alignas(", 0) == 0) {
                int depth = 0;
                std::size_t i = 0;
                for (; i < line.size(); ++i) {
                    if (line[i] == '(') ++depth;
                    if (line[i] == ')' && --depth == 0) break;
                }
                line = line.substr(std::min(line.size(), i + 2));
            }

            const std::size_t eq = line.find(" = nullptr;");
            if (eq != std::string::npos) line = line.substr(0, eq) + ";";
            os << "extern " << line << "\n";
        }

        return;
    }

    for (nodes::ASTNode* n : m_global_decls) {
        if (n->kind == K::VariableDeclaration) {
            auto* v = static_cast<nodes::VariableDeclaration*>(n);

            if (v->is_structured_binding()) {
                const std::string hidden = binding_global(v);
                const std::string cq = v->get_type_info().modifiers.has(RM::Const) ? "const " : "";
                if (v->binding_by_ref) os << cq << cpp_type(value_type(v->binding_source), v) << "* " << hidden << " = nullptr;\n";
                else os << "walnut_rt::late<" << cpp_type(value_type(v->binding_source), v) << "> " << hidden << ";\n";
                for (Sym* b : v->binding_symbols) os << cq << cpp_type(value_type(b->bound_type), v) << "* " << global_var_name(b) << " = nullptr;\n";
                continue;
            }

            if (!v->symbol || inline_variable(v) || pruned_global(v->symbol)) continue;
            Type* t = declared_type(v);
            if (t && t->is_reference()) {
                os << cpp_type(static_cast<semantics::ReferenceType*>(t)->referent(), v) << "* " << global_var_name(v->symbol) << " = nullptr;\n";
                continue;
            }
            if (is_closure_global(v->symbol)) {
                os << "inline auto " << global_var_name(v->symbol) << " = " << expr(v->m_initializer) << ";\n";
                continue;
            }
            const std::string late_type = "walnut_rt::late<" + cpp_type(m_types.strip_cv(value_type(t)), v) + ">";
            os << align_prefix(v->get_type_info().alignment, late_type) << late_type << " " << global_var_name(v->symbol) << ";\n";
        } else {
            auto* a = static_cast<nodes::ArrayDeclaration*>(n);
            if (!a->symbol) continue;
            const std::string late_type = "walnut_rt::late<" + cpp_type(array_decl_type(a), a) + ">";
            os << align_prefix(a->m_alignment, late_type) << late_type << " " << global_var_name(a->symbol) << ";\n";
        }
    }
}

auto CppEmitter::named_direct_base(RecordInfo* r, std::string_view name) -> RecordInfo* {
    if (!r || !r->decl || name.empty()) return nullptr;

    for (parser_types::TypeInfo* info : r->decl->all_bases()) {
        RecordInfo* b = record_info_of(m_types.canonicalize(*info));
        if (b && b->decl && b->decl->get_name() == name) return b;
    }

    return nullptr;
}

auto CppEmitter::init_record(RecordInfo* r, Sym* target) -> RecordInfo* {
    if (!r || !target) return nullptr;
    std::vector<parser_types::TypeInfo*> bases = r->decl->all_bases();

    for (parser_types::TypeInfo* info : bases) {
        RecordInfo* b = record_info_of(m_types.canonicalize(*info));
        if (b && (b->sym == target || info->resolved == target || (b->decl && b->decl->symbol == target))) return b;
    }

    if (target == r->sym || target == r->decl->symbol || (target->name == r->decl->get_name() && target->kind == SK::Type)) return r;
    return bases.size() == 1 ? record_info_of(m_types.canonicalize(*bases[0])) : nullptr;
}

std::string CppEmitter::interpolated_text(nodes::CastExpression* c) {
    Type* src = value_type(type_of(c->operand));
    const std::string v = expr(c->operand);

    if (src && src->is_record()) {
        Sym* chosen = nullptr;
        if (semantics::rank_user_defined(src, m_types.builtin(BK::String), m_types, false, &chosen) != semantics::ConversionRank::None && chosen) return "(" + v + ")." + function_name(chosen) + "()";
        if (semantics::rank_user_defined(src, m_types.builtin(BK::Text), m_types, false, &chosen) != semantics::ConversionRank::None && chosen) return "(" + v + ")." + function_name(chosen) + "()";
    }

    return "walnut_rt::to_display(" + v + ")";
}

std::string CppEmitter::type_test(nodes::CastExpression* c) {
    const bool is_test = c->cast_kind == nodes::CastExpression::CastKind::Is;
    Type* src = value_type(type_of(c->operand));

    if (c->special_test) {
        static const char* const tests[] = { "infinity", "infinity", "nan", "undefined", "indeterminate" };
        return "walnut_rt::is_special(" + expr(c->operand) + ", walnut_rt::special_test::" + tests[c->special_test < 5 ? c->special_test : 0] + ")";
    }

    Type* tv = m_types.strip_cv(m_types.canonicalize(c->get_target()));
    if (!src || !tv) { fail_at(c, "type test without types"); return "false"; }
    const std::string v = expr(c->operand);
    const std::string name = cpp_string_literal(type_text(tv));

    if (semantics::special_held_type(src) && !is_test) {
        return "walnut_rt::special_as<" + cpp_type(tv, c) + ">(" + v + ", " + name + ")";
    }

    if (src->kind() == semantics::TypeKind::Variant) {
        const std::string tc = cpp_type(tv, c);
        return is_test ? "std::holds_alternative<" + tc + ">(" + v + ")" : "walnut_rt::variant_as<" + tc + ">(" + v + ", " + name + ")";
    }

    if (is_dynamic(src)) {
        const std::string tc = cpp_type(tv, c);
        return is_test ? "walnut_rt::dynamic_is<" + tc + ">(" + v + ")" : "walnut_rt::dynamic_as<" + tc + ">(" + v + ", " + name + ")";
    }

    if (src->is_pointer() && tv->is_pointer()) {
        const std::string pc = cpp_type(m_types.strip_cv(static_cast<semantics::PointerType*>(tv)->pointee()), c);
        return is_test ? "walnut_rt::pointer_is<" + pc + ">(" + v + ")" : "walnut_rt::pointer_as<" + pc + ">(" + v + ", " + name + ")";
    }

    const std::string tc = cpp_type(tv, c);
    return is_test ? "walnut_rt::object_is<" + tc + ">(" + v + ")" : "walnut_rt::object_as<" + tc + ">(" + v + ", " + name + ")";
}

std::string CppEmitter::align_prefix(const parser_types::TemplateArgument* a, const std::string& cpp_natural) {
    if (!a || a->align_value <= 0) return {};
    return "alignas(" + std::to_string(a->align_value) + ") alignas(" + cpp_natural + ") ";
}

int CppEmitter::self_ref_kind(RecordInfo* r, const nodes::FunctionParameters* ps) {
    if (!ps || ps->size() != 1 || !(*ps)[0]) return 0;
    Type* t = m_types.canonicalize((*ps)[0]->get_type());
    if (!t || !t->is_reference()) return 0;
    if (record_info_of(value_type(t)) != r) return 0;
    return static_cast<semantics::ReferenceType*>(t)->ref_qual() == semantics::RefQual::RValue ? 2 : 1;
}

void CppEmitter::emit_special_members(std::ostream& os, RecordInfo* r, bool polymorphic) {
    using SP = nodes::ConstructorDeclaration::Special;
    int copy = 0;
    int move = 0;
    bool has_ctor = false;
    bool has_dtor = false;

    for (const auto& member : r->decl->get_members()) {
        nodes::ASTNode* n = member.node;
        if (!n) continue;
        if (n->kind == K::DestructorDeclaration) has_dtor = true;
        if (n->kind != K::ConstructorDeclaration) continue;
        auto* ctor = static_cast<nodes::ConstructorDeclaration*>(n);
        has_ctor = true;
        const int kind  = self_ref_kind(r, ctor->get_parameters());
        const int state = ctor->m_special == SP::Delete ? 2 : 1;
        if (kind == 1) copy = state;
        if (kind == 2) move = state;
    }

    const std::string& c = r->cname;
    if (!has_dtor && polymorphic) os << pad() << "virtual ~" << c << "() noexcept(false) {}\n";
    if (!has_dtor && !polymorphic && copy == 0 && move == 0) return;
    if (!has_ctor && !polymorphic) return;

    if (!has_ctor) os << pad() << c << "() = default;\n";
    if (copy == 0 && move != 1) os << pad() << c << "(const " << c << "&) = default;\n";

    if (copy == 2)      os << pad() << c << "& operator=(const " << c << "&) = delete;\n";
    else if (move != 1) os << pad() << c << "& operator=(const " << c << "&) = default;\n";

    if (move == 0 && copy != 1) {
        os << pad() << c << "(" << c << "&&) = default;\n";
        os << pad() << c << "& operator=(" << c << "&&) = default;\n";
    }
}

std::string CppEmitter::equality_helper(Type* t, const nodes::ASTNode* at) {
    RecordInfo* r = record_info_of(t);

    if (!r) {
        fail_at(at, "record type '" + type_text(t) + "' has no generated definition");
        return "false";
    }

    if (auto it = m_equality_names.find(r); it != m_equality_names.end()) return it->second;
    const std::string name = unique_global("eq_" + r->cname);
    m_equality_names[r] = name;
    m_equality_queue.push_back(r);
    return name;
}

void CppEmitter::emit_equality_helpers(std::ostream& os) {
    std::ostringstream protos, defs;

    for (std::size_t i = 0; i < m_equality_queue.size(); ++i) {
        RecordInfo* r = m_equality_queue[i];
        const std::string name = m_equality_names[r];
        const std::string head = "inline bool " + name + "(const " + r->cname + "& a, const " + r->cname + "& b)";
        protos << head << ";\n";
        std::vector<std::string> terms;

        for (const nodes::RecordDeclaration::EqualityStep& step : r->decl->equality_steps) {
            std::string lhs, rhs;

            if (!step.field) {
                RecordInfo* base = step.base ? record_info_of(step.base) : nullptr;
                if (!base) { fail_at(r->decl, "base class of '" + r->cname + "' has no generated definition"); continue; }
                lhs = "static_cast<const " + base->cname + "&>(a)";
                rhs = "static_cast<const " + base->cname + "&>(b)";
            } else {
                const std::string field = step.field->kind == K::ArrayDeclaration
                    ? local_name(static_cast<nodes::ArrayDeclaration*>(step.field)->m_name)
                    : local_name(static_cast<nodes::VariableDeclaration*>(step.field)->get_name());
                lhs = "a." + field;
                rhs = "b." + field;
            }

            if (step.op) {
                const bool member = owner_record_of(step.op) != nullptr;
                const bool is_const = step.op->decl && step.op->decl->kind == K::OperatorFunctionDeclaration
                    && static_cast<const nodes::OperatorFunctionDeclaration*>(step.op->decl)->qualifiers().has(FQ::Const);

                if (member) {
                    const std::string recv = is_const ? lhs : "const_cast<std::remove_const_t<std::remove_reference_t<decltype(" + lhs + ")>>&>(" + lhs + ")";
                    terms.push_back("static_cast<bool>((" + recv + ")." + function_name(step.op) + "(" + rhs + "))");
                } else {
                    terms.push_back("static_cast<bool>(" + function_name(step.op) + "(" + lhs + ", " + rhs + "))");
                }
            } else if (step.nested) {
                terms.push_back(equality_helper(step.nested, r->decl) + "(" + lhs + ", " + rhs + ")");
            } else {
                terms.push_back("(" + lhs + " == " + rhs + ")");
            }
        }

        defs << head << " {\n    return ";
        if (terms.empty()) defs << "true";

        for (std::size_t t = 0; t < terms.size(); ++t) {
            if (t) defs << "\n        && ";
            defs << terms[t];
        }

        defs << ";\n}\n\n";
    }

    os << protos.str();
    if (!m_equality_queue.empty()) os << "\n";
    os << defs.str();
}

bool CppEmitter::is_lazy_decl(const nodes::ASTNode* n) {
    return n && n->kind == K::VariableDeclaration && static_cast<const nodes::VariableDeclaration*>(n)->get_type_info().modifiers.has(RM::Lazy);
}

bool CppEmitter::is_lazy_field(const Sym* s) {
    return s && s->kind == SK::Variable && s->owner && s->owner->kind == semantics::Scope::Kind::Record && is_lazy_decl(s->decl);
}

std::string CppEmitter::lazy_prefix(nodes::ASTNode* target) {
    if (target && target->kind == K::MemberAccessExpression) {
        auto* m = static_cast<nodes::MemberAccessExpression*>(target);
        if (RecordInfo* q = qualifier_record(m)) return member_object(m) + q->cname + "::";
        return member_object(m);
    }
    return "this->";
}

void CppEmitter::emit_lazy_fields(std::ostream& os, RecordInfo* r) {
    for (const auto& member : r->decl->get_members()) {
        if (!is_lazy_decl(member.node)) continue;
        auto* v = static_cast<nodes::VariableDeclaration*>(member.node);
        const std::string ct = cpp_type(m_types.strip_cv(m_types.canonicalize(v->get_type_info())), v);
        const bool ts = v->get_type_info().modifiers.has(RM::LazyThreadsafe);
        const std::string n = local_name(v->get_name());
        os << pad() << "mutable walnut_rt::lazy_cell<" << ct << ", " << (ts ? "true" : "false") << "> " << n << ";\n";
        os << pad() << ct << "& lz_" << n << "() const;\n";
        os << pad() << ct << "& lz_set_" << n << "(" << ct << " walnut_value) const;\n";
        os << pad() << ct << " lz_make_" << n << "();\n";
    }
}

void CppEmitter::emit_lazy_definitions(std::ostream& os, RecordInfo* r) {
    for (const auto& member : r->decl->get_members()) {
        if (!is_lazy_decl(member.node)) continue;
        auto* v = static_cast<nodes::VariableDeclaration*>(member.node);
        Type* t = m_types.strip_cv(m_types.canonicalize(v->get_type_info()));
        const std::string ct = cpp_type(t, v);
        const std::string n = local_name(v->get_name());
        const std::string label = "\"" + std::string(v->get_name()) + "\"";
        os << ct << "& " << r->cname << "::lz_" << n << "() const { return " << n << ".get([this]() -> " << ct << " { return const_cast<" << r->cname << "*>(this)->lz_make_" << n << "(); }, " << label << "); }\n";
        os << ct << "& " << r->cname << "::lz_set_" << n << "(" << ct << " walnut_value) const { return " << n << ".set(std::move(walnut_value), " << label << "); }\n";
        line_marker(os, v);
        Type* saved_ret = m_current_return;
        m_current_return = t;
        m_indent = 0;
        os << ct << " " << r->cname << "::lz_make_" << n << "() {\n    return " << conv(v->m_initializer, t, v) << ";\n}\n";
        m_current_return = saved_ret;
        line_reset(os);
        os << "\n";
    }
}

bool CppEmitter::prunable_global(nodes::ASTNode* n) const {
    if (!n || n->kind != K::VariableDeclaration || !source_path) return false;
    auto* v = static_cast<nodes::VariableDeclaration*>(n);
    if (!v->symbol || v->is_structured_binding()) return false;
    const auto& mods = v->get_type_info().modifiers;
    if (!mods.has(RM::Const) && !mods.has(RM::Constexpr)) return false;
    return source_path(FileId(v->file_id)).rfind(SourceManager::LIBRARY_ROOT, 0) == 0;
}

void CppEmitter::note_global_uses(nodes::ASTNode* n) {
    if (!n) return;
    switch (n->kind) {
        case K::Identifier:             if (Sym* s = static_cast<nodes::Identifier*>(n)->resolved) m_used_globals.insert(s); break;
        case K::QualifiedIdentifier:    if (Sym* s = static_cast<nodes::QualifiedIdentifier*>(n)->resolved) m_used_globals.insert(s); break;
        case K::MemberAccessExpression: if (Sym* s = static_cast<nodes::MemberAccessExpression*>(n)->resolved) m_used_globals.insert(s); break;
        default: break;
    }
    nodes::for_each_child<true>(n, [this](nodes::ASTNode* c) { note_global_uses(c); });
}

void CppEmitter::finish_global_usage() {
    m_track_globals = false;
    std::vector<nodes::VariableDeclaration*> waiting;

    for (nodes::ASTNode* n : m_script) {
        if (n->kind == K::VariableDeclaration) {
            auto* v = static_cast<nodes::VariableDeclaration*>(n);
            if (v->symbol && m_prunable.count(v->symbol) && !m_used_globals.count(v->symbol)) { waiting.push_back(v); continue; }
            note_global_uses(v->m_initializer);
        } else {
            note_global_uses(n);
        }
    }

    for (bool changed = true; changed;) {
        changed = false;
        for (nodes::VariableDeclaration*& v : waiting) {
            if (!v || !m_used_globals.count(v->symbol)) continue;
            note_global_uses(v->m_initializer);
            v = nullptr;
            changed = true;
        }
    }

    m_prune_ready = true;
}

void CppEmitter::emit_definitions(std::ostream& os) {
    for (RecordInfo* r : m_record_order) emit_record_definitions(os, r);
    for (const FuncInfo& f : m_functions) emit_function_body(os, f);
}

void CppEmitter::emit_init(std::ostream& os) {
    os << "void walnut_init() {\n";
    m_indent = 1;
    RecordInfo* saved = m_current_record;
    m_current_record = nullptr;
    m_current_return = m_types.void_();

    for (nodes::ASTNode* n : m_script) {
        if (n->kind == K::VariableDeclaration) {
            auto* v = static_cast<nodes::VariableDeclaration*>(n);

            if (v->is_structured_binding()) {
                const std::string hidden = binding_global(v);
                std::string base;
                if (v->binding_by_ref) { os << pad() << hidden << " = &(" << expr(v->m_initializer) << ");\n"; base = "(*" + hidden + ")"; }
                else { os << pad() << hidden << ".init(" << conv(v->m_initializer, v->binding_source, v) << ");\n"; base = hidden + ".get()"; }
                std::vector<std::string> parts = binding_parts(v->binding_source, v->binding_symbols.size(), v);
                Type* src = value_type(v->binding_source);
                if (src && src->is_array() && !static_cast<semantics::ArrayType*>(src)->extent()) os << pad() << "walnut_rt::check_binding_count(" << base << ", " << v->binding_symbols.size() << ");\n";
                for (std::size_t i = 0; i < v->binding_symbols.size() && i < parts.size(); ++i) os << pad() << global_var_name(v->binding_symbols[i]) << " = &(" << base << parts[i] << ");\n";
                continue;
            }

            if (!v->symbol || is_closure_global(v->symbol) || inline_variable(v) || pruned_global(v->symbol)) continue;
            Type* t = declared_type(v);

            if (t && t->is_reference()) {
                os << pad() << global_var_name(v->symbol) << " = &(" << (v->m_initializer ? expr(v->m_initializer) : std::string("*static_cast<int*>(nullptr)")) << ");\n";
                continue;
            }

            std::string init;
            if (v->m_initializer && v->get_type_info().modifiers.has(RM::Constexpr) && v->m_initializer->kind != K::BraceInitializerList) init = folded_or(v, t, true);
            else if (v->m_initializer) init = direct_init_args(v->m_initializer, t, v);
            os << pad() << global_var_name(v->symbol) << ".init(" << init << ");\n";
        } else if (n->kind == K::ArrayDeclaration) {
            auto* a = static_cast<nodes::ArrayDeclaration*>(n);
            if (!a->symbol) continue;
            Type* t = array_decl_type(a);
            const std::string ai = array_init(a, t);
            os << pad() << global_var_name(a->symbol) << ".init(" << (!ai.empty() && ai[0] == '{' ? cpp_type(t, a) + ai : ai) << ");\n";
        } else {
            statement(os, n);
        }
    }

    m_current_record = saved;
    m_indent = 0;
    os << "}\n";
    line_reset(os);
    os << "\n";
}

} // namespace codegen
} // namespace walnut
