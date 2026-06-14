#ifndef WALNUT_DRIVER_LINKER_HPP
#define WALNUT_DRIVER_LINKER_HPP

#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>

#include "front_pass.hpp"
#include "module_loader.hpp"

#include "../Common/error_reporter.hpp"
#include "../Common/arena_allocator.hpp"
#include "../Parser/nodes.hpp"

#include "../Semantics/scope.hpp"
#include "../Semantics/symbol.hpp"
#include "../Semantics/resolve.hpp"
#include "../Semantics/import_resolver.hpp"  

namespace walnut {
namespace driver {

class Linker {
public:
    Linker(ErrorReporter& reporter, Arena& arena) noexcept : m_reporter(reporter), m_arena(arena) {}

    void run(const std::unordered_map<FileId, UnitFrontResult>& results, const std::unordered_map<FileId, std::unordered_map<std::string, FileId>>& file_edges) {
        m_results    = &results;
        m_file_edges = &file_edges;
        std::vector<Pending> pending = collect();
        bool progress = true;

        while (progress && !pending.empty()) {
            progress = false;
            std::vector<Pending> next;

            for (Pending& p : pending) {
                if (try_bind(p)) { progress = true; }  
                else             { next.push_back(p); }  
            }

            pending.swap(next);
        }

        for (const Pending& p : pending) { report(p, "unresolved import (cyclic or unresolvable re-export)"); }
    }

private:
    using IK = nodes::ImportExportItem::Kind;

    struct Pending {
        FileId                                importer;
        const nodes::ImportExportItem*        item;
        const nodes::ImportExportDeclaration* decl;  
    };

    ErrorReporter& m_reporter;
    Arena&         m_arena;
    const std::unordered_map<FileId, UnitFrontResult>*                         m_results    = nullptr;
    const std::unordered_map<FileId, std::unordered_map<std::string, FileId>>* m_file_edges = nullptr;

private:
    const UnitFrontResult* unit(FileId f) const {
        auto it = m_results->find(f);
        return it == m_results->end() ? nullptr : &it->second;
    }

    void report(const Pending& p, const std::string& msg) {
        m_reporter.report(ErrorPhase::Semantic, p.importer, p.decl->line, msg);
    }

    std::vector<Pending> collect() {
        std::vector<Pending> out;

        for (const auto& kv : *m_results) {
            const UnitFrontResult& u = kv.second;
            if (!u.ast) continue;

            for (nodes::ASTNode* stmt : u.ast->get_statements()) {
                if (stmt->kind != nodes::ASTNode::Kind::ImportExportDeclaration) continue;
                auto* d = static_cast<nodes::ImportExportDeclaration*>(stmt);
                if (!d->is_import()) continue;
                for (const nodes::ImportExportItem& it : d->get_items()) { out.push_back(Pending{ u.file, &it, d }); }
            }
        }

        return out;
    }

    static const std::vector<std::string_view>& local_parts(const nodes::ImportExportItem& it) {
        if (it.kind == IK::File) return it.alias_parts;
        return it.has_alias ? it.alias_parts : it.target_parts;  
    }

    static std::vector<std::string_view> export_public_parts(const semantics::ExportEntry& e) {
        if (e.kind == IK::Function && e.decl && e.decl->kind == nodes::ASTNode::Kind::FunctionDeclaration) { return { static_cast<const nodes::FunctionDeclaration*>(e.decl)->get_name() }; }
        return e.public_parts;
    }

    static std::vector<std::string_view> export_origin_parts(const semantics::ExportEntry& e) {
        if (e.kind == IK::Function && e.decl && e.decl->kind == nodes::ASTNode::Kind::FunctionDeclaration) { return { static_cast<const nodes::FunctionDeclaration*>(e.decl)->get_name() }; }
        return e.origin_parts.empty() ? e.public_parts : e.origin_parts;
    }

    const semantics::ExportEntry* find_export(const UnitFrontResult& tgt, const std::vector<std::string_view>& name, IK kind) {
        for (const semantics::ExportEntry& e : tgt.exports) {
            const bool kind_ok = (e.kind == kind) || (kind == IK::Name && e.kind == IK::Function); 
            if (kind_ok && export_public_parts(e) == name) return &e;
        }

        return nullptr;
    }

    semantics::Symbol* resolve_export(const UnitFrontResult& tgt, const semantics::ExportEntry& e, bool& defer) {
        defer = false;
        semantics::Symbol* s = semantics::resolve_qualified(tgt.root, export_origin_parts(e));

        while (s && s->is_imported) {
            if (!s->import_target) { defer = true; return nullptr; }
            s = s->import_target;
        }

        return s;
    }

    bool try_bind(const Pending& p) {
        const nodes::ImportExportItem& it = *p.item;
        const UnitFrontResult* imp = unit(p.importer);
        if (!imp || !imp->root) return true;
        if (!it.has_source) { report(p, "import has no source"); return true; }
        FileId target = INVALID_FILE;

        if (auto fe = m_file_edges->find(p.importer); fe != m_file_edges->end()) {
            if (auto s = fe->second.find(std::string(it.source)); s != fe->second.end()) target = s->second;
        }

        if (target == INVALID_FILE) { report(p, "cannot resolve import source \"" + std::string(it.source) + "\""); return true; }
        const UnitFrontResult* tgt = unit(target);
        if (!tgt || !tgt->root) { report(p, "import target failed to compile"); return true; }
        semantics::Symbol* ph = semantics::resolve_qualified(imp->root, local_parts(it));
        if (!ph || !ph->is_imported) return true;
        if (it.kind == IK::File) return bind_file(p, ph, *tgt);
        return bind_named(p, it, ph, *tgt);
    }

    bool bind_named(const Pending& p, const nodes::ImportExportItem& it, semantics::Symbol* ph, const UnitFrontResult& tgt) {
        if (ph->import_target) return true;   
        semantics::Symbol* resolved = nullptr;

        if (it.kind == IK::Module) {
            resolved = semantics::resolve_qualified(tgt.root, it.target_parts);

            while (resolved && resolved->is_imported) {
                if (!resolved->import_target) return false;
                resolved = resolved->import_target;
            }

            if (!resolved) {
                report(p, "target unit does not declare module '" + std::string(it.target_parts.empty() ? std::string_view{} : it.target_parts.back()) + "'");
                return true;
            }
        } else { 
            const semantics::ExportEntry* e = find_export(tgt, it.target_parts, it.kind);

            if (!e) {
                report(p, "'" + std::string(it.target_parts.empty() ? std::string_view{} : it.target_parts.back()) + "' is not exported by the target unit");
                return true;
            }

            bool defer = false;
            resolved = resolve_export(tgt, *e, defer);
            if (defer)     return false;
            if (!resolved) { report(p, "export resolves to an undefined declaration"); return true; }
        }

        ph->import_target = resolved;
        ph->type          = resolved->type;
        if (resolved->inner_scope) ph->inner_scope = resolved->inner_scope;
        return true;
    }

    bool bind_file(const Pending& p, semantics::Symbol* ph, const UnitFrontResult& tgt) {
        if (ph->inner_scope) return true;   

        for (const semantics::ExportEntry& e : tgt.exports) {
            if (!e.module_owner.empty()) continue;       
            bool defer = false;
            resolve_export(tgt, e, defer);
            if (defer) return false;
        }

        semantics::Scope* ns = make_in<semantics::Scope>(m_arena, semantics::Scope::Kind::Namespace, ph->owner);

        for (const semantics::ExportEntry& e : tgt.exports) {
            if (!e.module_owner.empty()) continue;
            bool defer = false;
            semantics::Symbol* r = resolve_export(tgt, e, defer);
            if (!r) continue;                             
            std::vector<std::string_view> name = export_public_parts(e);
            if (name.empty()) continue;
            semantics::Symbol* alias = make_in<semantics::Symbol>(m_arena, name.back(), r->kind, r->decl);
            alias->is_imported   = true;
            alias->import_target = r;
            alias->type          = r->type;
            alias->inner_scope   = r->inner_scope;
            ns->declare(alias);
        }
        
        ph->inner_scope = ns;
        return true;
    }
};

} // namespace driver
} // namespace walnut

#endif // WALNUT_DRIVER_LINKER_HPP