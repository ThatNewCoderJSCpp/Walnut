#include "Semantics/unused_checker.hpp"
#include "Parser/Nodes/node_children.hpp"

namespace walnut {
namespace semantics {

namespace {

using K = nodes::ASTNode::Kind;

bool ignored_name(std::string_view name) { return name.empty() || name.front() == '_'; }

} // namespace

void UnusedVariableChecker::run(FileId file, nodes::ASTNode* root) {
    m_file = file;
    find(root);
}

void UnusedVariableChecker::find(nodes::ASTNode* n) {
    if (!n) return;
    nodes::ASTNode* body = nullptr;
    switch (n->kind) {
        case K::FunctionDeclaration: body = static_cast<nodes::FunctionDeclaration*>(n)->m_body; break;
        case K::OperatorFunctionDeclaration: body = static_cast<nodes::OperatorFunctionDeclaration*>(n)->m_body; break;
        case K::ConstructorDeclaration: body = static_cast<nodes::ConstructorDeclaration*>(n)->m_body; break;
        case K::DestructorDeclaration: body = static_cast<nodes::DestructorDeclaration*>(n)->m_body; break;
        default: break;
    }
    if (body) {
        check_body(body);
        return;
    }
    nodes::for_each_child(n, [this](nodes::ASTNode* c) { find(c); });
}

void UnusedVariableChecker::check_body(nodes::ASTNode* body) {
    m_declared.clear();
    m_used.clear();
    m_unresolved.clear();
    collect(body, false);
    for (const Declared& d : m_declared) {
        bool used = false;
        for (const Symbol* s : d.symbols) used = used || (s && m_used.count(s));
        for (std::string_view name : d.names) used = used || m_unresolved.count(name);
        if (used) continue;
        std::string shown;
        for (std::string_view name : d.names) shown += (shown.empty() ? "" : ", ") + std::string(name);
        const bool many = d.names.size() > 1;
        ++m_warned;
        m_warnings.report(ErrorPhase::Semantic, m_file, d.line, std::string(many ? "variables '" : "variable '") + shown + (many ? "' are" : "' is") + " defined but never used");
    }
}

void UnusedVariableChecker::collect(nodes::ASTNode* n, bool in_record) {
    if (!n) return;
    switch (n->kind) {
        case K::Identifier: {
            auto* id = static_cast<nodes::Identifier*>(n);
            if (id->resolved) m_used.insert(id->resolved);
            else m_unresolved.insert(id->name);
            break;
        }
        case K::QualifiedIdentifier: {
            auto* q = static_cast<nodes::QualifiedIdentifier*>(n);
            if (q->resolved) m_used.insert(q->resolved);
            break;
        }
        case K::RecordDeclaration:
            in_record = true;
            break;
        case K::LambdaExpression: {
            auto* l = static_cast<nodes::LambdaExpression*>(n);
            if (l->m_captures) for (auto& c : l->m_captures->m_captures) {
                if (c.resolved) m_used.insert(c.resolved);
                if (!c.name.empty()) m_unresolved.insert(c.name);
                collect(c.init, in_record);
            }
            break;
        }
        case K::VariableDeclaration: {
            auto* v = static_cast<nodes::VariableDeclaration*>(n);
            if (in_record || v->synthetic) break;
            Declared d;
            d.line = v->line;
            if (v->is_structured_binding()) {
                bool skip = false;
                for (std::string_view b : v->get_bindings()) {
                    skip = skip || ignored_name(b);
                    d.names.push_back(b);
                }
                for (Symbol* s : v->binding_symbols) d.symbols.push_back(s);
                if (skip) break;
            } else {
                if (ignored_name(v->get_name())) break;
                d.names.push_back(v->get_name());
                d.symbols.push_back(v->symbol);
            }
            m_declared.push_back(std::move(d));
            break;
        }
        case K::ArrayDeclaration: {
            auto* a = static_cast<nodes::ArrayDeclaration*>(n);
            if (in_record || ignored_name(a->m_name)) break;
            Declared d;
            d.line = a->line;
            d.names.push_back(a->m_name);
            d.symbols.push_back(a->symbol);
            m_declared.push_back(std::move(d));
            break;
        }
        default:
            break;
    }
    nodes::for_each_child<true>(n, [this, in_record](nodes::ASTNode* c) { collect(c, in_record); });
}

} // namespace semantics
} // namespace walnut
