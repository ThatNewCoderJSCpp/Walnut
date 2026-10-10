#include "Semantics/import_resolver.hpp"

namespace walnut {
namespace semantics {

void ImportResolver::run(nodes::BlockStatement* program) {
    Scope* root = m_ctx.root;   

    for (ASTNode* stmt : program->get_statements()) {
        switch (stmt->kind) {
            case ASTNode::Kind::ImportExportDeclaration:
                process_import_export(static_cast<nodes::ImportExportDeclaration*>(stmt), root, {});
                break;
            case ASTNode::Kind::ModuleDeclaration:
                process_module(static_cast<nodes::ModuleDeclaration*>(stmt));
                break;
            default:
                forbid_nested(stmt);
                break;
        }
    }
}

Scope* ImportResolver::namespace_path_parent(Scope* base, const std::vector<std::string_view>& parts, ASTNode* origin) {
    Scope* cur = base;
        
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        std::string_view part = parts[i];
        Symbol* sym = cur->find_local(part);

        if (sym && (sym->kind == SymbolKind::Namespace || sym->kind == SymbolKind::Module) && sym->inner_scope) {
            cur = sym->inner_scope;
        } else if (sym) {
            SemanticError::redeclaration(m_reporter, origin->file_id, origin->line, part, sym->decl ? sym->decl->file_id : INVALID_FILE, sym->decl ? sym->decl->line : 0);
            return nullptr;
        } else {
            Symbol* ns       = make_in<Symbol>(m_arena, part, SymbolKind::Namespace, origin);
            ns->is_imported  = true;
            ns->is_hoisted   = true;    
            ns->inner_scope  = make_in<Scope>(m_arena, Scope::Kind::Namespace, cur);
            cur->declare(ns);
            cur = ns->inner_scope;
        }
    }

    return cur;
}

void ImportResolver::declare_import(Scope* target, std::string_view name, SymbolKind kind, ASTNode* origin, std::string_view source) {
    if (name.empty()) { return; }
    Symbol* sym        = make_in<Symbol>(m_arena, name, kind, origin);
    sym->is_imported   = true;
    sym->is_hoisted    = true; 
    sym->import_source = source;
        
    if (kind == SymbolKind::Namespace) { 
        sym->inner_scope = make_in<Scope>(m_arena, Scope::Kind::Namespace, target); 
    } else if (kind == SymbolKind::Module) { 
        sym->inner_scope = make_in<Scope>(m_arena, Scope::Kind::Module, target); 
    }

    if (Symbol* clash = target->declare(sym)) {
        SemanticError::redeclaration(m_reporter, origin->file_id, origin->line, name, clash->decl ? clash->decl->file_id : INVALID_FILE, clash->decl ? clash->decl->line : 0);
    }
}

void ImportResolver::import_item(const nodes::ImportExportItem& it, Scope* root, ASTNode* origin) {
    using IK = nodes::ImportExportItem::Kind;

    switch (it.kind) {
        case IK::Name: {
            const auto& parts = it.has_alias ? it.alias_parts : it.target_parts;
            Scope* parent = namespace_path_parent(root, parts, origin);
            if (parent && !parts.empty()) declare_import(parent, parts.back(), SymbolKind::Import, origin, it.source);
            break;
        }
        case IK::Namespace: {
            const auto& parts = it.has_alias ? it.alias_parts : it.target_parts;
            Scope* parent = namespace_path_parent(root, parts, origin);
            if (parent && !parts.empty()) declare_import(parent, parts.back(), SymbolKind::Namespace, origin, it.source);
            break;
        }
        case IK::File: {  
            Scope* parent = namespace_path_parent(root, it.alias_parts, origin);
            if (parent && !it.alias_parts.empty()) declare_import(parent, it.alias_parts.back(), SymbolKind::Import, origin, it.source);
            break;
        }
        case IK::Module: {
            declare_import(root, it.target_parts.empty() ? std::string_view{} : it.target_parts.back(), SymbolKind::Module, origin, it.source);
            break;
        }
        case IK::Function:  // parser rejects import-function
        case IK::This:      // &this is export-only
            break;
    }
}

void ImportResolver::export_item(const nodes::ImportExportItem& it, std::string_view module_owner) {
    ExportEntry e;
    e.kind          = it.kind;
    e.source        = it.source;
    e.has_source    = it.has_source;
    e.decl          = it.decl;
    e.module_owner  = module_owner;
    e.origin_parts  = it.target_parts;
    e.origin_global = it.target_global;

    if (it.has_alias) {
        e.public_parts  = it.alias_parts;
        e.public_global = it.alias_global;
    } else {
        e.public_parts  = it.target_parts;
        e.public_global = it.target_global;
    }

    m_exports.push_back(std::move(e));
}

void ImportResolver::process_import_export(nodes::ImportExportDeclaration* decl, Scope* root, std::string_view module_owner) {
    const bool is_import = decl->is_import();
        
    for (const auto& it : decl->get_items()) {
        if (is_import) import_item(it, root, decl);
        else           export_item(it, module_owner);
    }
}

void ImportResolver::forbid_nested(const ASTNode* node) {
    if (!node) { return; }
    using K = ASTNode::Kind;

    switch (node->kind) {
        case K::ImportExportDeclaration:
            SemanticError::import_export_not_global(m_reporter, node->file_id, node->line, static_cast<const nodes::ImportExportDeclaration*>(node)->is_import());
            return;
        case K::ModuleDeclaration:
            SemanticError::import_export_not_global(m_reporter, node->file_id, node->line, false);
            return;
        case K::BlockStatement:
            for (const ASTNode* s : static_cast<const nodes::BlockStatement*>(node)->get_statements()) forbid_nested(s);
            return;
        case K::NamespaceDeclaration: {
            auto* ns = static_cast<const nodes::NamespaceDeclaration*>(node);
            if (ns->get_body()) for (const ASTNode* s : ns->get_body()->get_statements()) forbid_nested(s);
            return;
        }
        case K::FunctionDeclaration: {
            auto* fn = const_cast<nodes::FunctionDeclaration*>(static_cast<const nodes::FunctionDeclaration*>(node));
            if (ASTNode* body = fn->get_body()) forbid_nested(body);   
            return;
        }
        case K::IfStatement: {
            auto* s = static_cast<const nodes::IfStatement*>(node);
            for (const nodes::IfBranch* br : s->get_branches()) forbid_nested(br->get_body());
            if (s->get_else()) forbid_nested(s->get_else());
            return;
        }
        case K::ForStatement:
            forbid_nested(static_cast<const nodes::ForStatement*>(node)->get_body());
            return;
        case K::ForEachStatement:
            forbid_nested(static_cast<const nodes::ForEachStatement*>(node)->get_body());
            return;
        case K::WhileStatement:
            forbid_nested(static_cast<const nodes::WhileStatement*>(node)->get_body());
            return;
        case K::DoWhileStatement:
            forbid_nested(static_cast<const nodes::DoWhileStatement*>(node)->get_body());
            return;
        case K::SwitchStatement: {
            auto* s = static_cast<const nodes::SwitchStatement*>(node);
            for (const nodes::SwitchCase* c : s->get_cases()) for (const ASTNode* stmt : c->get_body()) forbid_nested(stmt);
            return;
        }
        case K::TryCatchStatement: {
            auto* s = static_cast<const nodes::TryCatchStatement*>(node);
            forbid_nested(s->get_try_body());
            for (const nodes::TryCatchStatement* h = s; h; h = h->next_handler) if (h->get_catch_body()) forbid_nested(h->get_catch_body());
            return;
        }
        default:
            return;
    }
}

} // namespace semantics
} // namespace walnut
