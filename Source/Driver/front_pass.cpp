#include "Driver/front_pass.hpp"
#include "Semantics/semantic_error.hpp"
#include <deque>
#include <functional>
#include <sstream>

namespace walnut {
namespace driver {

void FrontPass::run(const std::unordered_map<FileId, ParsedUnit>& units) {
    std::vector<FileId> order;
    order.reserve(units.size());
    for (const auto& kv : units) order.push_back(kv.first);
    std::sort(order.begin(), order.end());

    for (FileId id : order) {
        const ParsedUnit& unit = units.at(id);
        if (!unit.ast) continue;          
        process(unit);
    }
}

void FrontPass::hoist_module_functions(nodes::BlockStatement* ast) {
    std::vector<nodes::ASTNode*>& stmts = ast->statements;

    for (std::size_t i = 0; i < stmts.size(); ++i) {
        if (!stmts[i] || stmts[i]->kind != nodes::ASTNode::Kind::ModuleDeclaration) continue;
        auto* mod = static_cast<nodes::ModuleDeclaration*>(stmts[i]);
        std::size_t at = i + 1;

        for (const nodes::ImportExportItem& it : mod->get_items()) {
            if (it.kind != nodes::ImportExportItem::Kind::Function || !it.decl) continue;
            stmts.insert(stmts.begin() + static_cast<std::ptrdiff_t>(at), it.decl);
            ++at;
        }

        i = at - 1;
    }
}

namespace {

using NK = nodes::ASTNode::Kind;

struct ClassEntry {
    std::vector<std::string_view> path;
    nodes::RecordDeclaration*     decl = nullptr;
};

std::string type_text(const parser_types::TypeInfo& t) {
    std::ostringstream os;
    os << t;
    return os.str();
}

bool same_signature(nodes::FunctionDeclaration* a, nodes::FunctionDeclaration* b) {
    const nodes::FunctionParameters* pa = a->get_parameters();
    const nodes::FunctionParameters* pb = b->get_parameters();
    const std::size_t na = pa ? pa->m_params.size() : 0;
    const std::size_t nb = pb ? pb->m_params.size() : 0;
    if (na != nb) return false;
    for (std::size_t i = 0; i < na; ++i) if (type_text(pa->m_params[i]->get_type()) != type_text(pb->m_params[i]->get_type())) return false;
    return a->qualifiers().has(modifiers::FunctionQualifiers::Const) == b->qualifiers().has(modifiers::FunctionQualifiers::Const);
}

void collect_classes(const std::vector<nodes::ASTNode*>& stmts, std::vector<std::string_view>& prefix, std::vector<ClassEntry>& out) {
    for (nodes::ASTNode* s : stmts) {
        if (!s) continue;

        if (s->kind == NK::RecordDeclaration) {
            auto* rd = static_cast<nodes::RecordDeclaration*>(s);
            if (rd->m_is_declaration || rd->m_name_parts.empty()) continue;
            prefix.push_back(rd->m_name_parts.back());
            out.push_back({ prefix, rd });
            std::vector<nodes::ASTNode*> nested;
            for (const auto& m : rd->get_members()) nested.push_back(m.node);
            collect_classes(nested, prefix, out);
            prefix.pop_back();
        } else if (s->kind == NK::NamespaceDeclaration) {
            auto* ns = static_cast<nodes::NamespaceDeclaration*>(s);
            if (!ns->get_body()) continue;
            for (std::string_view p : ns->name_parts()) prefix.push_back(p);
            collect_classes(ns->get_body()->statements, prefix, out);
            for (std::size_t i = 0; i < ns->name_parts().size(); ++i) prefix.pop_back();
        }
    }
}

std::string joined(const std::vector<std::string_view>& parts, std::size_t count) {
    std::string out;
    for (std::size_t i = 0; i < count && i < parts.size(); ++i) { if (i) out += "::"; out += std::string(parts[i]); }
    return out;
}

}

void FrontPass::lower_template_operator(nodes::ASTNode* s) {
    static std::deque<std::string> names;
    if (!s) return;

    if (s->kind == NK::NamespaceDeclaration) {
        auto* ns = static_cast<nodes::NamespaceDeclaration*>(s);
        if (ns->get_body()) lower_template_operators(ns->get_body()->statements);
        return;
    }

    if (s->kind == NK::RecordDeclaration) {
        for (auto& m : static_cast<nodes::RecordDeclaration*>(s)->m_members) lower_template_operator(m.node);
        return;
    }

    if (s->kind != NK::TemplateDeclaration) return;
    auto* tmpl = static_cast<nodes::TemplateDeclaration*>(s);
    if (!tmpl->m_declaration) return;

    if (tmpl->m_declaration->kind == NK::RecordDeclaration) {
        lower_template_operator(tmpl->m_declaration);
        return;
    }

    if (tmpl->m_declaration->kind != NK::OperatorFunctionDeclaration) return;
    auto* op = static_cast<nodes::OperatorFunctionDeclaration*>(tmpl->m_declaration);
    if (op->is_conversion() || !op->has_body()) return;
    const std::string& name = names.emplace_back("operator(" + std::string(nodes::overloadable_operator_name(op->get_overload())) + ")");
    auto* fd = m_arena.alloc<nodes::FunctionDeclaration>(std::string_view(name), op->get_return_type(), op->get_modifiers(), op->qualifiers(), op->line, op->get_parameters(), op->get_body());
    fd->file_id = op->file_id;
    fd->order = op->order;
    fd->m_lowered_operator = op->get_overload();
    tmpl->m_declaration = fd;
}

void FrontPass::lower_template_operators(std::vector<nodes::ASTNode*>& stmts) {
    for (nodes::ASTNode* s : stmts) lower_template_operator(s);
}

void FrontPass::attach_member_definitions(nodes::BlockStatement* ast) {
    std::vector<ClassEntry> classes;
    std::vector<std::string_view> prefix;
    collect_classes(ast->statements, prefix, classes);
    if (classes.empty()) return;

    std::function<void(std::vector<nodes::ASTNode*>&, std::vector<std::string_view>&)> visit = [&](std::vector<nodes::ASTNode*>& stmts, std::vector<std::string_view>& where) {
        for (std::size_t i = 0; i < stmts.size(); ) {
            nodes::ASTNode* s = stmts[i];

            if (s && s->kind == NK::NamespaceDeclaration) {
                auto* ns = static_cast<nodes::NamespaceDeclaration*>(s);
                if (ns->get_body()) {
                    for (std::string_view p : ns->name_parts()) where.push_back(p);
                    visit(ns->get_body()->statements, where);
                    for (std::size_t k = 0; k < ns->name_parts().size(); ++k) where.pop_back();
                }
                ++i;
                continue;
            }

            if (!s || s->kind != NK::FunctionDeclaration) { ++i; continue; }
            auto* def = static_cast<nodes::FunctionDeclaration*>(s);
            const std::vector<std::string_view>& parts = def->name_parts();
            if (parts.size() < 2 || !def->has_body()) { ++i; continue; }
            const std::vector<std::string_view> qualifier(parts.begin(), parts.end() - 1);
            ClassEntry* owner = nullptr;

            for (std::size_t keep = def->is_global_qualified() ? 0 : where.size() + 1; keep-- > 0 && !owner; ) {
                std::vector<std::string_view> path(where.begin(), where.begin() + static_cast<std::ptrdiff_t>(keep));
                path.insert(path.end(), qualifier.begin(), qualifier.end());
                for (ClassEntry& c : classes) if (c.path == path) { owner = &c; break; }
            }

            if (!owner) { ++i; continue; }
            nodes::FunctionDeclaration* proto = nullptr;
            bool name_found = false;

            for (auto& m : owner->decl->m_members) {
                if (!m.node || m.node->kind != NK::FunctionDeclaration) continue;
                auto* cand = static_cast<nodes::FunctionDeclaration*>(m.node);
                if (cand->get_name() != parts.back()) continue;
                name_found = true;
                if (!same_signature(cand, def)) continue;

                if (cand->has_body()) {
                    semantics::SemanticError::emit(m_reporter, def->file_id, def->line, "Definition", "'" + joined(parts, parts.size()) + "' is already defined inside the class");
                    proto = cand;
                    break;
                }

                def->m_name_parts = { parts.back() };
                def->m_is_global_qualified = false;
                def->m_qualifiers.add_mask(cand->qualifiers().flags());
                def->m_modifiers.add_mask(cand->get_modifiers().flags());
                m.node = def;
                proto = cand;
                break;
            }

            if (!proto) {
                const std::string cls = joined(qualifier, qualifier.size());
                if (name_found) semantics::SemanticError::emit(m_reporter, def->file_id, def->line, "Definition", "none of the 'function " + std::string(parts.back()) + "' declarations in '" + cls + "' has the same parameters and 'const' as this definition");
                else semantics::SemanticError::emit(m_reporter, def->file_id, def->line, "Definition", "'" + cls + "' declares no member function '" + std::string(parts.back()) + "'; declare it inside the class first, as 'function " + std::string(parts.back()) + "(...) -> ...;'");
                ++i;
                continue;
            }

            stmts.erase(stmts.begin() + static_cast<std::ptrdiff_t>(i));
        }
    };

    std::vector<std::string_view> where;
    visit(ast->statements, where);
}

void FrontPass::process(const ParsedUnit& unit) {
    ScopedFile _f(m_reporter, unit.file);
    hoist_module_functions(unit.ast);
    attach_member_definitions(unit.ast);
    lower_template_operators(unit.ast->statements);
    semantics::AnalysisContext ctx(m_arena, m_reporter);
    semantics::ScopeBuilder builder(ctx);
    semantics::Scope* root = builder.build(unit.ast);
    semantics::ImportResolver resolver(ctx);
    resolver.run(unit.ast);
    UnitFrontResult res;
    res.file    = unit.file;
    res.ast     = unit.ast;
    res.root    = root;
    res.exports = resolver.exports();
    m_results.emplace(unit.file, std::move(res));
    m_order.push_back(unit.file);
}

} // namespace driver
} // namespace walnut
