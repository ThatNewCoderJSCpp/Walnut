#ifndef WALNUT_CODEGEN_CPP_EMITTER_HPP
#define WALNUT_CODEGEN_CPP_EMITTER_HPP

#include "../Semantics/type_impl.hpp"
#include "../Semantics/conversion.hpp"
#include "../Semantics/overload.hpp"
#include "../Semantics/instantiator.hpp"
#include "../Semantics/const_evaluator.hpp"
#include "../Semantics/prelude.hpp"
#include "../Semantics/scope.hpp"
#include "../Semantics/symbol.hpp"
#include "../Parser/nodes.hpp"
#include "../Lexer/escapes.hpp"
#include "../Common/error_reporter.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace walnut {
namespace codegen {

struct CodegenUnit {
    FileId                 file = INVALID_FILE;
    nodes::BlockStatement* ast  = nullptr;
    semantics::Scope*      root = nullptr;
};

class CppEmitter {
    using K   = nodes::ASTNode::Kind;
    using TK  = tokenizing::Token::Kind;
    using BK  = parser_types::PrimitiveType::BaseKind;
    using RM  = modifiers::RawModifiers;
    using FQ  = modifiers::FunctionQualifiers;
    using Sym = semantics::Symbol;
    using SK  = semantics::SymbolKind;
    using Type = semantics::Type;
    using Inst = semantics::Instantiator::Instantiation;

public:
    CppEmitter(semantics::TypeContext& types, semantics::Instantiator& inst, ErrorReporter& reporter)
        : m_types(types), m_inst(inst), m_reporter(reporter), m_eval(types)
    {
        m_eval.env_of = [this](Sym* s) -> const semantics::SubstEnv* {
            Inst* I = m_inst.owning(s);
            return I ? &I->env : nullptr;
        };
    }

    CppEmitter(const CppEmitter&) = delete;
    CppEmitter& operator=(const CppEmitter&) = delete;

    bool uses_coroutines() const { return m_uses_coroutines; }

    bool emit(const std::vector<CodegenUnit>& units, std::ostream& out) {
        collect(units);
        if (m_failed) return false;
        if (!m_main_fn && m_script.empty()) {
            fail_at(nullptr, "program has no entry point: define 'function main() -> int' or write top-level statements");
            return false;
        }

        std::ostringstream records, protos, globals, defs, init;
        emit_records(records);
        emit_prototypes(protos);
        emit_globals(globals);
        emit_definitions(defs);
        emit_init(init);
        if (m_failed) return false;

        out << "#include \"walnut_runtime.hpp\"\n\n";
        out << "namespace wlu {\n\n";
        for (const RecordInfo* r : m_record_order) out << "struct " << r->cname << ";\n";
        if (!m_record_order.empty()) out << "\n";
        out << m_enums.str();
        out << m_consts.str() << (m_consts.str().empty() ? "" : "\n");
        out << records.str();
        out << protos.str() << "\n";
        out << globals.str() << "\n";
        out << defs.str();
        out << init.str();
        out << "} // namespace wlu\n\n";
        out << "int main(int argc, char** argv) {\n";
        out << "    return walnut_rt::run_program([&]() -> int {\n";
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
        return !m_failed;
    }

private:
    struct RecordInfo {
        nodes::RecordDeclaration* decl = nullptr;
        Sym*                      sym  = nullptr;
        Inst*                     inst = nullptr;
        Inst*                     env  = nullptr;
        std::string               cname;
    };

    struct FuncInfo {
        nodes::ASTNode*           decl  = nullptr;
        Sym*                      sym   = nullptr;
        Inst*                     inst  = nullptr;
        RecordInfo*               owner = nullptr;
    };

    struct ParamSlot {
        Type*                          type = nullptr;
        nodes::FunctionParameter*      param = nullptr;
        bool                           group = false;
    };

    semantics::TypeContext&   m_types;
    semantics::Instantiator&  m_inst;
    ErrorReporter&            m_reporter;
    semantics::ConstEvaluator m_eval;
    bool                      m_failed = false;

    std::vector<std::unique_ptr<RecordInfo>>      m_records;
    std::vector<RecordInfo*>                      m_record_order;
    std::unordered_map<const Sym*, RecordInfo*>   m_record_by_sym;
    std::vector<FuncInfo>                         m_functions;
    std::vector<nodes::EnumDeclaration*>          m_enum_decls;
    std::vector<nodes::ASTNode*>                  m_script;
    std::vector<nodes::ASTNode*>                  m_global_decls;
    Sym*                                          m_main_fn = nullptr;

    std::unordered_map<const Sym*, std::string>   m_name_cache;
    std::set<std::string>                         m_used_global_names;
    std::ostringstream                            m_enums;
    std::ostringstream                            m_consts;
    std::map<std::string, std::string>            m_const_pool;
    int                                           m_temp = 0;

    bool                                          m_uses_coroutines = false;
    Type*                                         m_yield_type = nullptr;
    RecordInfo*                                   m_current_record = nullptr;
    bool                                          m_current_static = false;
    Type*                                         m_current_return = nullptr;
    int                                           m_indent = 0;

    void fail_at(const nodes::ASTNode* at, const std::string& what) {
        m_failed = true;
        std::ostringstream ss;
        const std::size_t line = at ? at->line : 0;
        ss << "Codegen Error on line " << line << ": " << what;
        m_reporter.report(ErrorPhase::Semantic, at ? FileId(at->file_id) : INVALID_FILE, line, ss.str());
    }

    void unsupported(const nodes::ASTNode* at, const std::string& what) {
        fail_at(at, what + " is not supported by the code generator yet");
    }

    struct EnvScope {
        semantics::TypeContext& t; bool on;
        EnvScope(semantics::TypeContext& tc, const semantics::SubstEnv* e) : t(tc), on(e != nullptr) { if (on) t.push_subst(e); }
        ~EnvScope() { if (on) t.pop_subst(); }
    };

    static bool is_template_generic(const nodes::ASTNode* n) { return n && n->kind == K::TemplateDeclaration; }

    void collect_record(nodes::RecordDeclaration* rd, Inst* inst, Inst* env = nullptr) {
        Sym* sym = inst ? inst->sym : rd->symbol;
        if (!sym || rd->m_is_declaration) return;
        if (m_record_by_sym.count(sym)) return;
        auto info = std::make_unique<RecordInfo>();
        info->decl = rd;
        info->sym  = sym;
        info->inst = inst;
        info->env  = inst ? inst : env;
        info->cname = unique_global(std::string("R_") + sanitize(qualified_name(sym)) + (inst ? "_" + mangle_args(inst->args) : ""));
        RecordInfo* raw = info.get();
        m_records.push_back(std::move(info));
        m_record_by_sym[sym] = raw;

        for (const auto& member : rd->get_members()) {
            nodes::ASTNode* n = member.node;
            if (!n) continue;

            switch (n->kind) {
                case K::FunctionDeclaration: {
                    auto* f = static_cast<nodes::FunctionDeclaration*>(n);
                    if (f->get_modifiers().has(RM::Friend)) break;
                    if (f->has_body() && f->symbol) m_functions.push_back(FuncInfo{ n, f->symbol, raw->env, raw });
                    break;
                }
                case K::OperatorFunctionDeclaration: {
                    auto* f = static_cast<nodes::OperatorFunctionDeclaration*>(n);
                    if (f->get_modifiers().has(RM::Friend)) break;
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

    void collect_decl(nodes::ASTNode* n, bool top_level) {
        if (!n) return;

        switch (n->kind) {
            case K::FunctionDeclaration: {
                auto* f = static_cast<nodes::FunctionDeclaration*>(n);
                if (f->has_body() && f->symbol) {
                    m_functions.push_back(FuncInfo{ n, f->symbol, nullptr, nullptr });
                    if (f->get_name() == "main" && top_level && !m_main_fn) m_main_fn = f->symbol;
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

            case K::VariableDeclaration:
            case K::ArrayDeclaration:
                m_global_decls.push_back(n);
                m_script.push_back(n);
                break;

            case K::TemplateDeclaration:
            case K::ConceptDeclaration:
            case K::StaticAssertDeclaration:
            case K::UsingDeclaration:
            case K::ImportExportDeclaration:
            case K::ModuleDeclaration:
                break;

            default:
                if (top_level) m_script.push_back(n);
                else unsupported(n, "a statement inside a namespace");
                break;
        }
    }

    void collect(const std::vector<CodegenUnit>& units) {
        for (const CodegenUnit& u : units) {
            if (!u.ast) continue;
            for (nodes::ASTNode* s : u.ast->statements) collect_decl(s, true);
        }

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
                if (f->has_body() && I->sym) m_functions.push_back(FuncInfo{ I->decl, I->sym, I, owner_record_of(I->sym) });
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

        order_records();
    }

    void collect_locals(nodes::ASTNode* n, Inst* env) {
        if (!n) return;
        if (n->kind == K::RecordDeclaration) { collect_record(static_cast<nodes::RecordDeclaration*>(n), nullptr, env); return; }
        if (n->kind == K::EnumDeclaration) { m_enum_decls.push_back(static_cast<nodes::EnumDeclaration*>(n)); return; }
        if (n->kind == K::TemplateDeclaration) return;
        nodes::for_each_child(n, [&](nodes::ASTNode* c) { collect_locals(c, env); });
    }

    static bool contains_dependent_args(const std::vector<semantics::TemplateArg>& args) {
        for (const auto& a : args) {
            if (a.is_dependent_value()) return true;
            if (a.is_type && a.type && a.type->is_dependent()) return true;
        }
        return false;
    }

    RecordInfo* owner_record_of(Sym* s) {
        for (semantics::Scope* sc = s ? s->owner : nullptr; sc; sc = sc->parent) {
            if (sc->kind == semantics::Scope::Kind::Record && sc->owner_symbol) {
                auto it = m_record_by_sym.find(sc->owner_symbol);
                return it == m_record_by_sym.end() ? nullptr : it->second;
            }
            if (sc->kind == semantics::Scope::Kind::Function) return nullptr;
        }
        return nullptr;
    }

    void order_records() {
        std::unordered_map<RecordInfo*, int> state;
        std::function<void(RecordInfo*)> visit = [&](RecordInfo* r) {
            int& st = state[r];
            if (st == 2) return;
            if (st == 1) { fail_at(r->decl, "records '" + r->cname + "' contain each other by value"); return; }
            st = 1;
            EnvScope env(m_types, r->env ? &r->env->env : nullptr);

            if (r->decl->m_inherits.type) {
                if (RecordInfo* b = record_info_of(m_types.canonicalize(r->decl->m_inherits))) visit(b);
            }

            for (const auto& member : r->decl->get_members()) {
                if (!member.node || member.node->kind != K::VariableDeclaration) continue;
                auto* v = static_cast<nodes::VariableDeclaration*>(member.node);
                if (v->get_type_info().modifiers.has(RM::Static)) continue;
                for (RecordInfo* dep : value_dependencies(m_types.canonicalize(v->get_type_info()))) visit(dep);
            }

            st = 2;
            m_record_order.push_back(r);
        };

        for (auto& r : m_records) visit(r.get());
    }

    std::vector<RecordInfo*> value_dependencies(Type* t) {
        std::vector<RecordInfo*> out;
        t = m_types.strip_cv(t);
        if (!t) return out;
        if (t->is_record()) { if (RecordInfo* r = record_info_of(t)) out.push_back(r); }
        else if (t->is_array()) { auto inner = value_dependencies(static_cast<semantics::ArrayType*>(t)->element()); out.insert(out.end(), inner.begin(), inner.end()); }
        return out;
    }

    RecordInfo* record_info_of(Type* t) {
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

    static std::string sanitize(std::string_view s) {
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

    std::string unique_global(std::string base) {
        std::string name = base;
        int n = 2;
        while (m_used_global_names.count(name)) name = base + "_" + std::to_string(n++);
        m_used_global_names.insert(name);
        return name;
    }

    static std::string qualified_name(const Sym* s) {
        std::vector<std::string_view> parts;
        parts.push_back(s->name);

        for (const semantics::Scope* sc = s->owner; sc; sc = sc->parent) {
            if (sc->owner_symbol && (sc->kind == semantics::Scope::Kind::Namespace || sc->kind == semantics::Scope::Kind::Record)) parts.push_back(sc->owner_symbol->name);
        }

        std::string out;
        for (auto it = parts.rbegin(); it != parts.rend(); ++it) { if (!out.empty()) out += "::"; out += std::string(*it); }
        return out;
    }

    std::string type_mangle(Type* t) {
        std::ostringstream os;
        if (t) t->write_to(os); else os << "void";
        return sanitize(os.str());
    }

    std::string mangle_args(const std::vector<semantics::TemplateArg>& args) {
        std::string out;
        for (const auto& a : args) {
            if (!out.empty()) out += "_";
            if (a.is_type) out += type_mangle(a.type);
            else if (a.value) out += "v" + sanitize(a.value->to_string());
            else if (a.fvalue) { std::ostringstream os; os << *a.fvalue; out += "f" + sanitize(os.str()); }
        }
        return out;
    }

    static std::string local_name(std::string_view n) {
        return "v_" + sanitize(n);
    }

    semantics::Scope::Kind owner_kind(const Sym* s) const {
        return (s && s->owner) ? s->owner->kind : semantics::Scope::Kind::Module;
    }

    bool is_global_scope_owner(const Sym* s) const {
        const auto k = owner_kind(s);
        return k == semantics::Scope::Kind::Module || k == semantics::Scope::Kind::Namespace;
    }

    std::vector<Type*> param_types_of(const Sym* fn) {
        std::vector<Type*> out;
        const nodes::FunctionParameters* ps = params_of(fn ? fn->decl : nullptr);
        if (!ps) return out;
        Inst* I = m_inst.owning(const_cast<Sym*>(fn));
        EnvScope env(m_types, I ? &I->env : nullptr);
        for (const nodes::FunctionParameter* p : ps->m_params) out.push_back(m_types.canonicalize(p->get_type()));
        return out;
    }

    static const nodes::FunctionParameters* params_of(const nodes::ASTNode* d) {
        if (!d) return nullptr;
        switch (d->kind) {
            case K::FunctionDeclaration:         return static_cast<const nodes::FunctionDeclaration*>(d)->get_parameters();
            case K::OperatorFunctionDeclaration: return static_cast<const nodes::OperatorFunctionDeclaration*>(d)->get_parameters();
            case K::ConstructorDeclaration:      return static_cast<const nodes::ConstructorDeclaration*>(d)->get_parameters();
            default:                             return nullptr;
        }
    }

    std::string signature_mangle(const Sym* fn) {
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

    std::string operator_word(const nodes::OperatorFunctionDeclaration* op) {
        if (op->is_conversion()) return "conv_" + type_mangle(m_types.canonicalize(op->get_conversion_type()));
        return "op_" + sanitize(nodes::overloadable_operator_name(op->get_overload()));
    }

    std::string operator_word_named(const nodes::OperatorFunctionDeclaration* op) {
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

    const Sym* definition_of(const Sym* fn) {
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

    std::string function_name(const Sym* fn) {
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

    std::string global_var_name(const Sym* s) {
        if (auto it = m_name_cache.find(s); it != m_name_cache.end()) return it->second;
        std::string n = unique_global("g_" + sanitize(qualified_name(s)));
        m_name_cache[s] = n;
        return n;
    }

    std::string enum_name(const Sym* s) {
        if (auto it = m_name_cache.find(s); it != m_name_cache.end()) return it->second;
        std::string n = unique_global("E_" + sanitize(qualified_name(s)));
        m_name_cache[s] = n;
        return n;
    }

    static bool is_void(Type* t) {
        return t && t->is_builtin() && static_cast<semantics::BuiltinType*>(t)->is_void();
    }

    static bool is_dynamic(Type* t) {
        return t && t->is_builtin() && static_cast<semantics::BuiltinType*>(t)->is_dynamic();
    }

    static semantics::BuiltinType* as_builtin(Type* t) {
        return (t && t->is_builtin()) ? static_cast<semantics::BuiltinType*>(t) : nullptr;
    }

    Type* value_type(Type* t) {
        if (!t) return nullptr;
        if (t->is_reference()) t = static_cast<semantics::ReferenceType*>(t)->referent();
        return m_types.strip_cv(t);
    }

    std::string builtin_cpp(semantics::BuiltinType* b, const nodes::ASTNode* at) {
        const unsigned bits = bit_width_of_rank(b->width());

        switch (b->base()) {
            case BK::Int:     return std::string("walnut_rt::") + (b->is_unsigned() ? "u" : "i") + std::to_string(bits);
            case BK::Float:   return "walnut_rt::f" + std::to_string(bits);
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

    std::string cpp_type(Type* t, const nodes::ASTNode* at = nullptr) {
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
                std::string s = "std::variant<";
                for (std::size_t i = 0; i < v->alternatives().size(); ++i) { if (i) s += ", "; s += cpp_type(v->alternatives()[i], at); }
                return s + ">";
            }
            default:
                fail_at(at, "type '" + type_text(t) + "' cannot be generated");
                return "int";
        }
    }

    static std::string type_text(Type* t) {
        std::ostringstream os;
        if (t) t->write_to(os); else os << "<none>";
        return os.str();
    }

    Type* type_of(const nodes::ASTNode* n) { return n ? n->expr_type.type : nullptr; }

    bool is_record_t(Type* t) { t = value_type(t); return t && t->is_record(); }

    std::string convert(const std::string& code, Type* from, Type* to, const nodes::ASTNode* at, bool explicit_cast = false) {
        Type* f = value_type(from);
        Type* t = value_type(to);
        if (!t || !f) return code;
        if (f == t) return code;
        if (t->is_error() || f->is_error()) return code;

        if (is_dynamic(t)) return "walnut_rt::dynamic(" + code + ")";
        if (is_dynamic(f)) return "walnut_rt::from_dynamic<" + cpp_type(t, at) + ">(" + code + ")";

        auto* fb = as_builtin(f);
        auto* tb = as_builtin(t);

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

    nodes::LambdaExpression* closure_spec_for(Type* closure, semantics::FunctionType* sig) {
        auto* gl = static_cast<nodes::LambdaExpression*>(static_cast<semantics::ClosureType*>(closure)->lambda());
        std::vector<Type*> key;
        for (Type* p : sig->params()) key.push_back(value_type(p));
        for (std::size_t i = 0; i < gl->spec_keys.size(); ++i) if (gl->spec_keys[i] == key && !gl->spec_failed[i]) return gl->specializations[i];
        return nullptr;
    }

    std::string closure_adapter(const std::string& code, nodes::LambdaExpression* spec, semantics::FunctionType* sig, const nodes::ASTNode* at) {
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

    std::string functor_adapter(const std::string& code, Sym* op, semantics::FunctionType* sig, const nodes::ASTNode* at) {
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

    std::string folded_or(nodes::VariableDeclaration* v, Type* t, bool prefer_constant) {
        using RM = modifiers::RawModifiers;
        Type* vt = value_type(t);
        auto* b = as_builtin(vt);
        const bool scalar = (b && (b->base() == BK::Int || b->base() == BK::Float || b->base() == BK::Bool || b->base() == BK::Char)) || (vt && vt->is_enum());

        if (scalar && (prefer_constant || v->get_type_info().modifiers.has(RM::Constexpr))) {
            semantics::ConstValue cv = m_eval.eval(v->m_initializer);
            if (cv.ok() && !cv.is_bool() == !(b && b->base() == BK::Bool)) {
                if (vt->is_enum() && cv.is_int()) return "static_cast<" + cpp_type(vt, v) + ">(" + cv.i->to_string() + "LL)";
                if (!vt->is_enum()) return constant_value_code(cv, vt, v);
            }
        }

        return conv(v->m_initializer, t, v);
    }

    std::string conv(nodes::ASTNode* n, Type* to, const nodes::ASTNode* at) {
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
                if (lit->get_kind() == TK::Float || lit->get_kind() == TK::Integer) return float_literal((negative ? "-" : "") + std::string(lit->value), t, at);
            }
        }

        return convert(expr(n), type_of(n), to, at);
    }

    std::string pool_constant(const std::string& type, const std::string& text) {
        const std::string key = type + "|" + text;
        if (auto it = m_const_pool.find(key); it != m_const_pool.end()) return it->second;
        std::string name = "k_" + std::to_string(m_const_pool.size());
        m_const_pool[key] = name;
        m_consts << "static const " << type << " " << name << " = " << type << "(std::string(\"" << text << "\"));\n";
        return name;
    }

    static std::string cpp_string_literal(const std::string& bytes) {
        std::string out = "std::string(\"";
        for (unsigned char c : bytes) {
            if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
            else if (c >= 32 && c < 127) out += static_cast<char>(c);
            else { char buf[8]; std::snprintf(buf, sizeof buf, "\\x%02x\"\"", c); out += buf; }
        }
        out += "\", " + std::to_string(bytes.size()) + ")";
        return out;
    }

    std::string int_literal(const WideInt& v, Type* t, const nodes::ASTNode* at) {
        const std::string type = cpp_type(t, at);
        const std::string digits = v.to_string();
        const bool fits = digits.size() < 18;
        if (fits) return type + "(" + digits + "LL)";
        return pool_constant(type, digits);
    }

    std::string float_literal(const std::string& text, Type* t, const nodes::ASTNode* at) {
        return pool_constant(cpp_type(t, at), text);
    }

    std::string constant_value_code(const semantics::ConstValue& v, Type* t, const nodes::ASTNode* at) {
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
        return "0";
    }

    std::string literal(nodes::Literal* lit) {
        Type* t = type_of(lit);

        switch (lit->get_kind()) {
            case TK::Integer: {
                semantics::ConstValue v = m_eval.eval(lit);
                if (!v.is_int()) { fail_at(lit, "integer literal could not be evaluated"); return "0"; }
                return int_literal(*v.i, t, lit);
            }
            case TK::Float:          return float_literal(std::string(lit->value), t, lit);
            case TK::InfinityKeyword: return "walnut_rt::cast<" + cpp_type(t, lit) + ">(walnut_rt::f2048(std::string(\"inf\")))";
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

    std::string symbol_ref(Sym* s, const nodes::ASTNode* at) {
        if (!s) { fail_at(at, "unresolved name"); return "0"; }

        switch (s->kind) {
            case SK::Variable:
            case SK::Parameter: {
                const auto k = owner_kind(s);
                if (k == semantics::Scope::Kind::Module || k == semantics::Scope::Kind::Namespace) {
                    if (is_reference_global(s) || is_binding_symbol(s)) return "(*" + global_var_name(s) + ")";
                    if (is_closure_global(s)) return global_var_name(s);
                    return global_var_name(s) + ".get()";
                }

                if (k == semantics::Scope::Kind::Record) {
                    RecordInfo* r = owner_record_of(s);
                    const bool is_static = s->decl && s->decl->kind == K::VariableDeclaration && static_cast<nodes::VariableDeclaration*>(s->decl)->get_type_info().modifiers.has(RM::Static);
                    if (is_static && r) return r->cname + "::" + local_name(s->name);
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
                if (s->intrinsic) return semantics::intrinsic_runtime_name(static_cast<semantics::Intrinsic>(s->intrinsic));
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

    std::string expr(nodes::ASTNode* e) {
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
                return "(*" + expr(d->operand) + ")";
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
                if (c->cast_kind == CK::Reinterpret) return "reinterpret_cast<" + cpp_type(to, e) + ">(" + expr(c->operand) + ")";
                if (c->cast_kind == CK::Const)       return "const_cast<" + cpp_type(to, e) + ">(" + expr(c->operand) + ")";
                if (c->cast_kind == CK::Dynamic)     return "dynamic_cast<" + cpp_type(m_types.canonicalize(c->get_target()), e) + ">(" + expr(c->operand) + ")";
                if (c->conversion) return "(" + expr(c->operand) + ")." + function_name(c->conversion) + "()";
                return convert(expr(c->operand), type_of(c->operand), to, e, true);
            }

            case K::NewExpression: {
                auto* n = static_cast<nodes::NewExpression*>(e);
                Type* allocated = m_types.canonicalize(n->type);
                if (n->is_array) return "new " + cpp_type(allocated, e) + "[walnut_rt::to_ll(" + expr(n->array_size) + ")]()";
                return "new " + cpp_type(allocated, e) + "(" + call_args(n->ctor, n->args, e) + ")";
            }

            case K::DeleteExpression: {
                auto* d = static_cast<nodes::DeleteExpression*>(e);
                return std::string(d->is_array ? "delete[] " : "delete ") + expr(d->operand);
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
                semantics::ConstValue v = m_eval.eval(e);
                if (v.ok()) return constant_value_code(v, type_of(e), e);

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

    static const char* node_kind_name(const nodes::ASTNode* n) {
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

    std::string fold(nodes::FoldExpression* f) {
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

    std::string truthy(nodes::ASTNode* cond) {
        Type* t = value_type(type_of(cond));
        if (t && t->is_builtin() && as_builtin(t)->is_bool()) return expr(cond);
        if (t && t->is_record()) {
            Type* b = m_types.bool_();
            return convert(expr(cond), t, b, cond);
        }
        return "walnut_rt::truthy(" + expr(cond) + ")";
    }

    Type* common_type(Type* a, Type* b) {
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

    static const char* arith_helper(TK op) {
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

    static bool is_string_t(Type* t) {
        auto* b = as_builtin(t);
        return b && (b->base() == BK::String || b->base() == BK::Text);
    }

    static bool is_char_t(Type* t) {
        auto* b = as_builtin(t);
        return b && b->base() == BK::Char;
    }

    static bool has_effects(const nodes::ASTNode* n) {
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

    static int effect_class(const nodes::ASTNode* n) {
        if (!n || n->kind == K::Literal) return 0;
        return has_effects(n) ? 2 : 1;
    }

    std::string invoke(const std::string& target, const std::vector<std::string>& codes, const std::vector<int>& classes) {
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
            const bool by_ref = i < classes.size() && (classes[i] & 4);
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

    std::string arith(TK op, nodes::ASTNode* l, nodes::ASTNode* r, Type* result, const nodes::ASTNode* at) {
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

    std::string call_operator_symbol(Sym* op, const std::vector<nodes::ASTNode*>& operands, const nodes::ASTNode* at) {
        if (operands.empty()) return "0";
        std::vector<Type*> params = param_types_of(op);
        std::vector<std::string> codes;
        std::vector<int> classes;

        if (owner_record_of(op)) {
            for (std::size_t i = 1; i < operands.size(); ++i) {
                Type* want = (i - 1) < params.size() ? params[i - 1] : nullptr;
                codes.push_back(want ? pass_arg(operands[i], want, at) : expr(operands[i]));
                classes.push_back(effect_class(operands[i]) | ((want && want->is_reference()) ? 4 : 0));
            }
            for (std::size_t i = operands.size() - 1; i < params.size(); ++i) { codes.push_back(cpp_type(params[i], at) + "{}"); classes.push_back(0); }
            return invoke("(" + expr(operands[0]) + ")." + function_name(op), codes, classes);
        }

        for (std::size_t i = 0; i < operands.size(); ++i) {
            Type* want = i < params.size() ? params[i] : nullptr;
            codes.push_back(want ? pass_arg(operands[i], want, at) : expr(operands[i]));
            classes.push_back(effect_class(operands[i]) | ((want && want->is_reference()) ? 4 : 0));
        }
        for (std::size_t i = operands.size(); i < params.size(); ++i) { codes.push_back(cpp_type(params[i], at) + "{}"); classes.push_back(0); }

        return invoke(function_name(op), codes, classes);
    }

    std::string pass_arg(nodes::ASTNode* arg, Type* param, const nodes::ASTNode* at) {
        if (param && param->is_reference()) {
            auto* r = static_cast<semantics::ReferenceType*>(param);
            Type* referent = m_types.strip_cv(r->referent());
            Type* from = value_type(type_of(arg));
            if (from == referent) return expr(arg);
            if (referent && referent->is_array() && !static_cast<semantics::ArrayType*>(referent)->extent()) return expr(arg);
            if (from && referent && from->is_record() && referent->is_record()) return expr(arg);
            return convert(expr(arg), from, referent, at);
        }
        return conv(arg, param, at);
    }

    std::string binary(nodes::BinaryExpression* b) {
        Type* result = type_of(b);

        if (b->resolved) {
            if (b->negate_result) return "(!" + call_operator_symbol(b->resolved, { b->left, b->right }, b) + ")";
            if (b->op != TK::Equal || is_record_t(type_of(b->left))) return call_operator_symbol(b->resolved, { b->left, b->right }, b);
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

    std::string unary(nodes::UnaryExpression* u) {
        if (u->resolved) return call_operator_symbol(u->resolved, { u->operand }, u);
        Type* rt = value_type(type_of(u));

        switch (u->op) {
            case TK::Minus:           return "walnut_rt::op_neg(" + conv(u->operand, rt, u) + ")";
            case TK::Plus:            return "walnut_rt::op_pos(" + conv(u->operand, rt, u) + ")";
            case TK::ExclamationMark: return "(!" + truthy(u->operand) + ")";
            case TK::DoublePlus:      return std::string(u->is_prefix() ? "walnut_rt::pre_inc(" : "walnut_rt::post_inc(") + expr(u->operand) + ")";
            case TK::DoubleMinus:     return std::string(u->is_prefix() ? "walnut_rt::pre_dec(" : "walnut_rt::post_dec(") + expr(u->operand) + ")";
            case TK::Asterisk:        return "(*" + expr(u->operand) + ")";
            case TK::Ampersand:       return "(&" + expr(u->operand) + ")";
            case TK::Tilde:           return "walnut_rt::op_bitnot(" + expr(u->operand) + ")";
            default:
                unsupported(u, "this unary operator");
                return "0";
        }
    }

    std::string subscript(nodes::SubscriptExpression* s) {
        if (s->resolved) return call_operator_symbol(s->resolved, { s->m_array, s->m_index }, s);
        Type* at = value_type(type_of(s->m_array));
        if (is_string_t(at)) return "walnut_rt::char_at(" + expr(s->m_array) + ", " + expr(s->m_index) + ")";
        return "walnut_rt::index(" + expr(s->m_array) + ", " + expr(s->m_index) + ")";
    }

    std::string member_access(nodes::MemberAccessExpression* m) {
        Sym* s = m->resolved;

        if (m->is_scope()) {
            if (!s) { fail_at(m, "unresolved scoped name"); return "0"; }
            if (s->kind == SK::Function) { unsupported(m, "taking a scoped function as a value"); return "0"; }
            return symbol_ref(s, m);
        }

        if (!s) { fail_at(m, "unresolved member '" + std::string(m->get_member()) + "'"); return "0"; }

        if (s->kind == SK::Variable && s->decl && s->decl->kind == K::VariableDeclaration
            && static_cast<nodes::VariableDeclaration*>(s->decl)->get_type_info().modifiers.has(RM::Static)) {
            if (RecordInfo* r = owner_record_of(s)) return r->cname + "::" + local_name(s->name);
        }

        const std::string sep = m->is_arrow() ? "->" : ".";
        return expr(m->m_object) + sep + local_name(s->name);
    }

    void call_arg_parts(Sym* fn, const std::vector<nodes::ASTNode*>& args, const nodes::ASTNode* at, std::vector<std::string>& codes, std::vector<int>& classes) {
        bool has_group = false;
        std::vector<ParamSlot> slots = param_slots(fn, has_group);
        if (has_group) { grouped_arg_parts(slots, args, at, codes, classes); return; }
        const nodes::FunctionParameters* ps = params_of(fn ? fn->decl : nullptr);
        std::vector<Type*> params = param_types_of(fn);
        const std::size_t n = std::max(args.size(), params.size());

        for (std::size_t i = 0; i < n; ++i) {
            if (i < args.size()) {
                codes.push_back(i < params.size() ? pass_arg(args[i], params[i], at) : expr(args[i]));
                classes.push_back(effect_class(args[i]) | ((i < params.size() && params[i] && params[i]->is_reference()) ? 4 : 0));
            } else if (ps && i < ps->m_params.size() && ps->m_params[i]->has_initializer()) {
                nodes::ASTNode* d = const_cast<nodes::ASTNode*>(ps->m_params[i]->get_initializer());
                codes.push_back(conv(d, params[i], at));
                classes.push_back(effect_class(d));
            }
        }
    }

    void grouped_arg_parts(const std::vector<ParamSlot>& slots, const std::vector<nodes::ASTNode*>& args, const nodes::ASTNode* at, std::vector<std::string>& codes, std::vector<int>& classes) {
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
            sh.has_default = s.param && !s.group && s.param->has_initializer();
            shapes.push_back(sh);
        }

        std::vector<std::size_t> takes;

        if (!semantics::assign_arguments(shapes, arg_types, m_types, takes)) {
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
                std::string code = cpp_type(m_types.array(el, std::nullopt), at) + "{";
                int cls = 0;
                std::size_t k = 0;
                for (; k < took && a < args.size(); ++k, ++a) {
                    if (k) code += ", ";
                    code += conv(args[a], el, at);
                    cls = std::max(cls, effect_class(args[a]));
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

            if (took && a < args.size()) {
                codes.push_back(pass_arg(args[a], s.type, at));
                classes.push_back(effect_class(args[a]) | ((s.type && s.type->is_reference()) ? 4 : 0));
                ++a;
            } else if (s.param && s.param->has_initializer()) {
                nodes::ASTNode* d = const_cast<nodes::ASTNode*>(s.param->get_initializer());
                codes.push_back(conv(d, s.type, at));
                classes.push_back(effect_class(d));
            }
        }
    }

    std::string call_args(Sym* fn, const std::vector<nodes::ASTNode*>& args, const nodes::ASTNode* at) {
        std::vector<std::string> codes;
        std::vector<int> classes;
        call_arg_parts(fn, args, at, codes, classes);
        std::string out;
        for (std::size_t i = 0; i < codes.size(); ++i) { if (i) out += ", "; out += codes[i]; }
        return out;
    }

    std::string invoke_with_args(const std::string& target, Sym* fn, const std::vector<nodes::ASTNode*>& args, const nodes::ASTNode* at) {
        std::vector<std::string> codes;
        std::vector<int> classes;
        call_arg_parts(fn, args, at, codes, classes);
        return invoke(target, codes, classes);
    }

    std::string aggregate_args(Type* t, const std::vector<nodes::ASTNode*>& els, const nodes::ASTNode* at) {
        RecordInfo* r = record_info_of(t);
        std::vector<Type*> fields;

        if (r) {
            EnvScope env(m_types, r->env ? &r->env->env : nullptr);
            for (const auto& member : r->decl->get_members()) {
                if (!member.node || member.node->kind != K::VariableDeclaration) continue;
                auto* v = static_cast<nodes::VariableDeclaration*>(member.node);
                if (v->get_type_info().modifiers.has(RM::Static)) continue;
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

    std::string call(nodes::CallExpression* c) {
        Sym* fn = c->resolved;

        if (fn && fn->intrinsic) {
            std::vector<std::string> codes;
            std::vector<int> classes;
            for (nodes::ASTNode* arg : c->m_arguments) {
                Type* t = value_type(type_of(arg));
                std::string a = expr(arg);
                if (t && t->is_enum()) a = "static_cast<long long>(" + a + ")";
                codes.push_back(a);
                classes.push_back(effect_class(arg));
            }
            return invoke(semantics::intrinsic_runtime_name(static_cast<semantics::Intrinsic>(fn->intrinsic)), codes, classes);
        }

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
                if (!cs || cs->kind == SK::Type || cs->kind == SK::TemplateParam || c->m_callee->kind == K::TemplateInstantiation) {
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
                    classes.push_back(effect_class(c->m_arguments[i]) | ((pt && pt->is_reference()) ? 4 : 0));
                }
                return invoke("(" + expr(c->m_callee) + ")", codes, classes);
            }

            if (ct && (ct->kind() == semantics::TypeKind::Function || is_record_t(ct))) {
                std::string out = "(" + expr(c->m_callee) + ")(";
                for (std::size_t i = 0; i < c->m_arguments.size(); ++i) { if (i) out += ", "; out += expr(c->m_arguments[i]); }
                return out + ")";
            }

            fail_at(c, "call target could not be resolved for code generation");
            return "0";
        }

        if (fn->decl && fn->decl->kind == K::OperatorFunctionDeclaration) {
            std::vector<nodes::ASTNode*> ops;
            ops.push_back(c->m_callee);
            for (nodes::ASTNode* a : c->m_arguments) ops.push_back(a);
            return call_operator_symbol(fn, ops, c);
        }

        RecordInfo* owner = owner_record_of(fn);
        std::string target;

        if (owner) {
            const bool is_static = fn->decl && fn->decl->kind == K::FunctionDeclaration && static_cast<nodes::FunctionDeclaration*>(fn->decl)->get_modifiers().has(RM::Static);

            if (c->m_callee->kind == K::MemberAccessExpression) {
                auto* m = static_cast<nodes::MemberAccessExpression*>(c->m_callee);
                if (m->is_scope() || is_static) target = owner->cname + "::" + function_name(fn);
                else target = expr(m->m_object) + (m->is_arrow() ? "->" : ".") + function_name(fn);
            } else if (is_static || c->m_callee->kind == K::QualifiedIdentifier) {
                target = owner->cname + "::" + function_name(fn);
            } else {
                target = "this->" + function_name(fn);
            }
        } else {
            target = function_name(fn);
        }

        return invoke_with_args(target, fn, c->m_arguments, c);
    }

    static bool is_generic_lambda(const nodes::LambdaExpression* l) {
        if (l->generic_origin || !l->get_parameters()) return false;
        for (const nodes::FunctionParameter* p : l->get_parameters()->m_params) {
            const auto& ti = p->get_type();
            if (ti.type && ti.type->is_primitive() && static_cast<parser_types::PrimitiveType*>(ti.type)->base_kind() == BK::Auto) return true;
        }
        return false;
    }

    std::string lambda(nodes::LambdaExpression* l) {
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

        if (auto* ps = l->get_parameters()) {
            for (std::size_t i = 0; i < ps->m_params.size(); ++i) {
                if (i) out += ", ";
                nodes::FunctionParameter* p = ps->m_params[i];
                out += cpp_type(m_types.canonicalize(p->get_type()), l) + " " + local_name(p->get_name());
            }
        }

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
        m_indent = saved_indent;
        m_current_return = saved;
        m_yield_type = saved_yield;
        return out;
    }

    struct LoopLabel {
        std::string name;
        bool        used = false;
    };

    std::vector<LoopLabel> m_loops;

    void loop_body(std::ostream& os, nodes::ASTNode* body) {
        m_loops.push_back(LoopLabel{ "walnut_redo_" + std::to_string(m_temp++) });
        std::ostringstream tmp;
        block(tmp, body);
        LoopLabel done = m_loops.back();
        m_loops.pop_back();
        std::string text = tmp.str();
        if (done.used) text.insert(2, pad() + "    " + done.name + ":;\n");
        os << text;
    }

    std::string pad() const { return std::string(static_cast<std::size_t>(m_indent) * 4, ' '); }

    static bool is_hoisted_function(const nodes::ASTNode* s) {
        return s && s->kind == K::FunctionDeclaration && static_cast<const nodes::FunctionDeclaration*>(s)->get_modifiers().has(RM::Hoisted);
    }

    static bool is_local_function(const Sym* s) {
        if (!s || s->kind != SK::Function || !s->owner) return false;
        const auto k = s->owner->kind;
        return k == semantics::Scope::Kind::Block || k == semantics::Scope::Kind::Function || k == semantics::Scope::Kind::Lambda;
    }

    std::string param_type_list(const nodes::FunctionParameters* ps, const nodes::ASTNode* at) {
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

    void nested_function(std::ostream& os, nodes::FunctionDeclaration* fd) {
        Sym* fs = fd->symbol;
        if (!fs || !fd->has_body()) return;
        if (fd->get_modifiers().has(RM::Static) || fd->qualifiers().has_any(FQ::Virtual | FQ::Override)) { fail_at(fd, "a function declared inside another function cannot be static or virtual"); return; }
        const std::string name = function_name(fs);
        Type* ret = return_type_of(fs);
        const bool async = fd->qualifiers().has(FQ::Async);
        const std::string rt = async ? "walnut_rt::task<" + cpp_type(ret, fd) + ">" : cpp_type(ret, fd);
        os << pad() << "std::function<" << rt << "(" << param_type_list(fd->get_parameters(), fd) << ")> " << name << ";\n";
        os << pad() << name << " = [&](" << param_list(fd->get_parameters(), fd) << ") -> " << rt << " ";
        std::vector<LoopLabel> saved_loops;
        saved_loops.swap(m_loops);
        callable_body(os, fd->get_body(), ret, async, "=", false);
        os << ";\n";
        m_loops.swap(saved_loops);
    }

    void block(std::ostream& os, nodes::ASTNode* b) {
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

    void field_names(RecordInfo* r, std::vector<std::string>& out, int depth = 0) {
        if (!r || depth > 32) return;
        EnvScope env(m_types, r->env ? &r->env->env : nullptr);
        if (r->decl->m_inherits.type) field_names(record_info_of(m_types.canonicalize(r->decl->m_inherits)), out, depth + 1);

        for (const auto& member : r->decl->get_members()) {
            nodes::ASTNode* n = member.node;
            if (!n) continue;
            if (n->kind == K::VariableDeclaration && !static_cast<nodes::VariableDeclaration*>(n)->get_type_info().modifiers.has(RM::Static)) out.push_back(local_name(static_cast<nodes::VariableDeclaration*>(n)->get_name()));
            if (n->kind == K::ArrayDeclaration && !static_cast<nodes::ArrayDeclaration*>(n)->get_array_modifiers().has(RM::Static)) out.push_back(local_name(static_cast<nodes::ArrayDeclaration*>(n)->m_name));
        }
    }

    std::vector<std::string> binding_parts(Type* source, std::size_t count, const nodes::ASTNode* at) {
        std::vector<std::string> parts;
        source = value_type(source);
        if (source && source->is_record()) field_names(record_info_of(source), parts);
        else for (std::size_t i = 0; i < count; ++i) parts.push_back("[" + std::to_string(i) + "]");
        if (parts.size() != count) fail_at(at, "structured binding does not match its source");
        for (std::string& p : parts) if (p[0] != '[') p = "." + p;
        return parts;
    }

    std::string binding_block(Type* source, bool by_ref, bool is_const, const std::string& init_code, const SmallVector<Sym*, 4>& symbols, const nodes::ASTNode* at) {
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

    static bool is_binding_symbol(const Sym* s) {
        return s && s->decl && s->decl->kind == K::VariableDeclaration && static_cast<const nodes::VariableDeclaration*>(s->decl)->is_structured_binding();
    }

    std::string local_decl(nodes::VariableDeclaration* v, bool with_semicolon = true) {
        if (!v->m_bindings.empty()) {
            if (!with_semicolon) { unsupported(v, "structured bindings in this position"); return ""; }
            const bool is_const = v->get_type_info().modifiers.has(RM::Const);
            std::string init = v->binding_by_ref ? expr(v->m_initializer) : conv(v->m_initializer, v->binding_source, v);
            return binding_block(v->binding_source, v->binding_by_ref, is_const, init, v->binding_symbols, v);
        }
        Type* t = m_types.canonicalize(v->get_type_info());
        if (prim_auto(v)) t = v->get_type_info().canonical;
        std::string out;
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

    static bool prim_auto(nodes::VariableDeclaration* v) {
        const auto& ti = v->get_type_info();
        if (!ti.type || !ti.type->is_primitive()) return false;
        return static_cast<parser_types::PrimitiveType*>(ti.type)->base_kind() == BK::Auto;
    }

    std::string brace_list(nodes::BraceInitializerList* list, Type* target, const nodes::ASTNode* at) {
        Type* t = value_type(target);
        Type* el = nullptr;
        if (t && t->is_array()) el = static_cast<semantics::ArrayType*>(t)->element();
        if (t && t->is_record() && !el) return cpp_type(t, at) + "{" + aggregate_args(t, list->m_elements, at) + "}";
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

    Type* array_decl_type(nodes::ArrayDeclaration* a) {
        Type* el = m_types.canonicalize(a->get_element_type());
        std::optional<std::size_t> extent = a->m_dimension;

        if (!extent && a->m_dimension_expr) {
            semantics::ConstValue v = m_eval.eval(a->m_dimension_expr);
            if (v.is_int()) extent = static_cast<std::size_t>(v.i->get_lowest_bits());
        }

        if (!extent && a->m_initializer) extent = a->m_initializer->m_elements.size();
        return m_types.array(el, extent, semantics::CV{});
    }

    std::string array_init(nodes::ArrayDeclaration* a, Type* t) {
        auto* at = (t && t->is_array()) ? static_cast<semantics::ArrayType*>(t) : nullptr;

        if (at && !at->extent() && a->m_dimension_expr) {
            std::string sized = "walnut_rt::sized_vector<" + cpp_type(at->element(), a) + ">(" + expr(a->m_dimension_expr) + ")";
            if (!a->m_initializer) return sized;
            return "walnut_rt::fill_from(" + sized + ", " + cpp_type(t, a) + brace_list(a->m_initializer, t, a) + ")";
        }

        if (a->m_initializer) return brace_list(a->m_initializer, t, a);
        return "{}";
    }

    int fold_condition(nodes::IfBranch* br) {
        if (br->is_constexpr) return br->constant_value;
        if (!br->condition || br->condition->kind == K::VariableDeclaration) return -1;
        semantics::ConstValue v = m_eval.eval(br->condition);
        bool ok = false;
        const bool t = semantics::ConstEvaluator::truthy(v, ok);
        return ok ? (t ? 1 : 0) : -1;
    }

    void if_statement(std::ostream& os, nodes::IfStatement* s) {
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

    void switch_statement(std::ostream& os, nodes::SwitchStatement* s) {
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

    void statement(std::ostream& os, nodes::ASTNode* s) {
        if (!s) return;

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
                os << pad() << local_decl(static_cast<nodes::VariableDeclaration*>(s)) << "\n";
                break;

            case K::ArrayDeclaration: {
                auto* a = static_cast<nodes::ArrayDeclaration*>(s);
                Type* t = array_decl_type(a);
                os << pad() << cpp_type(t, a) << " " << local_name(a->m_name) << " = " << array_init(a, t) << ";\n";
                break;
            }

            case K::CoReturnStatement:
                os << pad() << "co_return;\n";
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
                Type* et = m_types.canonicalize(f->m_element_type);
                const bool by_ref = et && et->is_reference();
                const std::string tmp = "walnut_item_" + std::to_string(m_temp++);
                Type* cont = value_type(type_of(f->m_container));
                Type* el = (cont && cont->is_array()) ? static_cast<semantics::ArrayType*>(cont)->element() : nullptr;
                if (cont && cont->kind() == semantics::TypeKind::Coroutine) el = static_cast<semantics::CoroutineType*>(cont)->value();
                std::string range = expr(f->m_container);
                if (is_string_t(cont)) { range = "walnut_rt::decode_utf8(" + range + ")"; el = m_types.builtin(BK::Char); }
                else if (is_dynamic(cont)) unsupported(f, "iterating over a dynamic value");
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
                switch (static_cast<nodes::SingleStatement*>(s)->variant) {
                    case V::Break:       os << pad() << "break;\n"; break;
                    case V::Continue:    os << pad() << "continue;\n"; break;
                    case V::Fallthrough: break;
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

            case K::RecordDeclaration:
            case K::EnumDeclaration:
                break;

            default:
                unsupported(s, "this statement");
                break;
        }
    }

    Type* return_type_of(Sym* fn) {
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

    std::string param_list(const nodes::FunctionParameters* ps, const nodes::ASTNode* at, bool with_defaults = false) {
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

            std::string piece = cpp_type(pt, at) + " " + local_name(p->get_name());
            if (with_defaults && p->has_initializer()) piece += " = " + conv(const_cast<nodes::ASTNode*>(p->get_initializer()), pt, at);
            add(piece);
        }

        return out;
    }

    std::vector<ParamSlot> param_slots(const Sym* fn, bool& has_group) {
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

    const semantics::SubstEnv* outer_env(const FuncInfo& f) const {
        return (f.owner && f.owner->env && f.owner->env != f.inst) ? &f.owner->env->env : nullptr;
    }

    std::string function_header(const FuncInfo& f, bool qualified, bool in_class) {
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

        if (in_class) {
            if (mods->has(RM::Static)) out += "static ";
            if (q->has(FQ::Virtual) || q->has(FQ::Override)) out += "virtual ";
        }

        out += (decl_is_async(f.decl) ? "walnut_rt::task<" + cpp_type(ret, f.decl) + ">" : cpp_type(ret, f.decl)) + " ";
        if (qualified && f.owner) out += f.owner->cname + "::";
        out += name + "(" + param_list(ps, f.decl) + ")";

        if (f.owner && q->has(FQ::Const) && !mods->has(RM::Static)) out += " const";
        return out;
    }

    void emit_function_body(std::ostream& os, const FuncInfo& f) {
        EnvScope outer(m_types, outer_env(f));
        EnvScope env(m_types, f.inst ? &f.inst->env : nullptr);
        RecordInfo* saved_rec = m_current_record;
        Type* saved_ret = m_current_return;
        m_current_record = f.owner;
        m_current_return = return_type_of(f.sym);
        nodes::ASTNode* body = nullptr;
        if (f.decl->kind == K::FunctionDeclaration) body = static_cast<nodes::FunctionDeclaration*>(f.decl)->get_body();
        else body = static_cast<nodes::OperatorFunctionDeclaration*>(f.decl)->m_body;
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
        os << "\n\n";
        m_current_record = saved_rec;
        m_current_return = saved_ret;
    }

    static bool decl_is_async(const nodes::ASTNode* d) {
        if (!d) return false;
        if (d->kind == K::FunctionDeclaration) return static_cast<const nodes::FunctionDeclaration*>(d)->qualifiers().has(FQ::Async);
        if (d->kind == K::OperatorFunctionDeclaration) return static_cast<const nodes::OperatorFunctionDeclaration*>(d)->qualifiers().has(FQ::Async);
        if (d->kind == K::LambdaExpression) return static_cast<const nodes::LambdaExpression*>(d)->get_qualifiers().has(FQ::Async);
        return false;
    }

    semantics::CoroutineType* generator_of(Type* t) {
        t = value_type(t);
        return (t && t->kind() == semantics::TypeKind::Coroutine && static_cast<semantics::CoroutineType*>(t)->is_generator()) ? static_cast<semantics::CoroutineType*>(t) : nullptr;
    }

    void callable_body(std::ostream& os, nodes::ASTNode* body, Type* ret, bool async, const std::string& caps, bool is_main) {
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

    void emit_enums() {
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

    semantics::ConstValue enum_value(nodes::EnumValue* v) {
        nodes::Identifier id(v->name, v->line);
        id.resolved = v->symbol;
        return m_eval.eval(&id);
    }

    void emit_record(std::ostream& os, RecordInfo* r) {
        EnvScope env(m_types, r->env ? &r->env->env : nullptr);
        RecordInfo* saved = m_current_record;
        m_current_record = r;
        os << "struct " << r->cname;

        if (r->decl->m_inherits.type) {
            Type* base = m_types.canonicalize(r->decl->m_inherits);
            if (RecordInfo* b = record_info_of(base)) os << " : public " << b->cname;
            else fail_at(r->decl, "base class of '" + r->cname + "' has no generated definition");
        }

        os << " {\n";
        m_indent = 1;
        bool has_virtual = false;

        for (const auto& member : r->decl->get_members()) {
            nodes::ASTNode* n = member.node;
            if (!n) continue;

            switch (n->kind) {
                case K::VariableDeclaration: {
                    auto* v = static_cast<nodes::VariableDeclaration*>(n);
                    Type* t = m_types.canonicalize(v->get_type_info());
                    const bool is_static = v->get_type_info().modifiers.has(RM::Static);
                    os << pad() << (is_static ? "static inline " : "") << cpp_type(m_types.strip_cv(t), v) << " " << local_name(v->get_name());
                    if (v->m_initializer) {
                        if (v->m_initializer->kind == K::BraceInitializerList) os << " = " << brace_list(static_cast<nodes::BraceInitializerList*>(v->m_initializer), t, v);
                        else os << " = " << folded_or(v, t, is_static);
                    } else if (!is_record_t(t)) {
                        os << "{}";
                    }
                    os << ";\n";
                    break;
                }

                case K::ArrayDeclaration: {
                    auto* a = static_cast<nodes::ArrayDeclaration*>(n);
                    Type* t = array_decl_type(a);
                    os << pad() << cpp_type(t, a) << " " << local_name(a->m_name) << " = " << array_init(a, t) << ";\n";
                    break;
                }

                case K::ConstructorDeclaration: {
                    auto* c = static_cast<nodes::ConstructorDeclaration*>(n);
                    using SP = nodes::ConstructorDeclaration::Special;
                    os << pad() << (c->m_qualifiers.has(FQ::Explicit) ? "explicit " : "") << r->cname << "(" << param_list(c->get_parameters(), c, true) << ")";
                    if (c->m_special == SP::Default) { os << " = default;\n"; break; }
                    if (c->m_special == SP::Delete)  { os << " = delete;\n"; break; }
                    os << ";\n";
                    break;
                }

                case K::DestructorDeclaration: {
                    auto* d = static_cast<nodes::DestructorDeclaration*>(n);
                    using SP = nodes::DestructorDeclaration::Special;
                    const bool virt = d->m_qualifiers.has(FQ::Virtual);
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

        {
            bool declared_dtor = false;
            for (const auto& member : r->decl->get_members()) if (member.node && member.node->kind == K::DestructorDeclaration) declared_dtor = true;
            if (!declared_dtor) os << pad() << (has_virtual ? "virtual " : "") << "~" << r->cname << "() noexcept(false) {}\n";
        }

        m_indent = 0;
        os << "};\n\n";
        m_current_record = saved;
    }

    void emit_record_definitions(std::ostream& os, RecordInfo* r) {
        EnvScope env(m_types, r->env ? &r->env->env : nullptr);
        RecordInfo* saved = m_current_record;
        m_current_record = r;

        for (const auto& member : r->decl->get_members()) {
            nodes::ASTNode* n = member.node;
            if (!n) continue;

            if (n->kind == K::ConstructorDeclaration) {
                auto* c = static_cast<nodes::ConstructorDeclaration*>(n);
                using SP = nodes::ConstructorDeclaration::Special;
                if (c->m_special != SP::None) continue;
                os << r->cname << "::" << r->cname << "(" << param_list(c->get_parameters(), c) << ")";
                std::vector<std::string> inits;

                for (nodes::ASTNode* init : c->get_init_list()) {
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
                        RecordInfo* b = r->decl->m_inherits.type ? record_info_of(m_types.canonicalize(r->decl->m_inherits)) : nullptr;
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
                os << "\n\n";
            } else if (n->kind == K::DestructorDeclaration) {
                auto* d = static_cast<nodes::DestructorDeclaration*>(n);
                using SP = nodes::DestructorDeclaration::Special;
                if (d->m_special != SP::None) continue;
                os << r->cname << "::~" << r->cname << "() noexcept(false) ";
                Type* saved_ret = m_current_return;
                m_current_return = m_types.void_();
                m_indent = 0;
                block(os, d->m_body);
                m_current_return = saved_ret;
                os << "\n\n";
            }
        }

        m_current_record = saved;
    }

    void emit_records(std::ostream& os) {
        emit_enums();
        for (RecordInfo* r : m_record_order) emit_record(os, r);
    }

    void emit_prototypes(std::ostream& os) {
        for (const FuncInfo& f : m_functions) {
            if (f.owner) continue;
            os << function_header(f, false, false) << ";\n";
        }
    }

    Type* declared_type(nodes::VariableDeclaration* v) {
        if (prim_auto(v)) return v->get_type_info().canonical;
        return m_types.canonicalize(v->get_type_info());
    }

    bool is_closure_global(const Sym* s) {
        if (!s || !s->decl || s->decl->kind != K::VariableDeclaration || !is_global_scope_owner(s)) return false;
        Type* t = value_type(declared_type(static_cast<nodes::VariableDeclaration*>(s->decl)));
        return t && t->kind() == semantics::TypeKind::Closure;
    }

    bool is_reference_global(const Sym* s) {
        if (!s || !s->decl || s->decl->kind != K::VariableDeclaration) return false;
        Type* t = declared_type(static_cast<nodes::VariableDeclaration*>(s->decl));
        return t && t->is_reference();
    }

    std::string direct_init_args(nodes::ASTNode* init, Type* t, const nodes::ASTNode* at) {
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

    std::unordered_map<const nodes::VariableDeclaration*, std::string> m_binding_globals;

    std::string binding_global(const nodes::VariableDeclaration* v) {
        auto it = m_binding_globals.find(v);
        if (it != m_binding_globals.end()) return it->second;
        std::string name = unique_global("g_bind");
        m_binding_globals[v] = name;
        return name;
    }

    void emit_globals(std::ostream& os) {
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

                if (!v->symbol) continue;
                Type* t = declared_type(v);
                if (t && t->is_reference()) {
                    os << cpp_type(static_cast<semantics::ReferenceType*>(t)->referent(), v) << "* " << global_var_name(v->symbol) << " = nullptr;\n";
                    continue;
                }
                if (is_closure_global(v->symbol)) {
                    os << "inline auto " << global_var_name(v->symbol) << " = " << expr(v->m_initializer) << ";\n";
                    continue;
                }
                os << "walnut_rt::late<" << cpp_type(m_types.strip_cv(value_type(t)), v) << "> " << global_var_name(v->symbol) << ";\n";
            } else {
                auto* a = static_cast<nodes::ArrayDeclaration*>(n);
                if (!a->symbol) continue;
                os << "walnut_rt::late<" << cpp_type(array_decl_type(a), a) << "> " << global_var_name(a->symbol) << ";\n";
            }
        }
    }

    void emit_definitions(std::ostream& os) {
        for (RecordInfo* r : m_record_order) emit_record_definitions(os, r);
        for (const FuncInfo& f : m_functions) emit_function_body(os, f);
    }

    void emit_init(std::ostream& os) {
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

                if (!v->symbol || is_closure_global(v->symbol)) continue;
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
        os << "}\n\n";
    }
};

} // namespace codegen
} // namespace walnut

#endif // WALNUT_CODEGEN_CPP_EMITTER_HPP
