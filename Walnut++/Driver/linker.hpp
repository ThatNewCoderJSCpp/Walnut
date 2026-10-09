#ifndef WALNUT_DRIVER_LINKER_HPP
#define WALNUT_DRIVER_LINKER_HPP

#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>
#include <cctype>

#include "front_pass.hpp"
#include "module_loader.hpp"

#include "../Common/error_reporter.hpp"
#include "../Common/arena_allocator.hpp"
#include "../Parser/nodes.hpp"

#include "../Semantics/scope.hpp"
#include "../Semantics/symbol.hpp"
#include "../Semantics/resolve.hpp"
#include "../Semantics/import_resolver.hpp"  
#include "../Semantics/semantic_error.hpp"

namespace walnut {
namespace driver {

class Linker {
public:
    Linker(ErrorReporter& reporter, Arena& arena, const SourceManager& manager) noexcept : m_reporter(reporter), m_arena(arena), m_sm(manager) {}

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

        for (const Pending& p : pending) {
            const std::vector<std::string_view>& parts = local_parts(*p.item);
            std::string name(parts.empty() ? std::string_view{} : parts.back());
            report(p, "unresolved import '" + name + "' from \"" + std::string(p.item->source) + "\" (cyclic or unresolvable re-export)");
        }
    }

private:
    using IK = nodes::ImportExportItem::Kind;

    struct Pending {
        FileId                                importer;
        const nodes::ImportExportItem*        item;
        const nodes::ImportExportDeclaration* decl;  
    };

    ErrorReporter&       m_reporter;
    Arena&               m_arena;
    const SourceManager& m_sm;
    const std::unordered_map<FileId, UnitFrontResult>*                         m_results    = nullptr;
    const std::unordered_map<FileId, std::unordered_map<std::string, FileId>>* m_file_edges = nullptr;

private:
    const UnitFrontResult* unit(FileId f) const {
        auto it = m_results->find(f);
        return it == m_results->end() ? nullptr : &it->second;
    }

    void report(const Pending& p, std::string_view msg) {
        semantics::SemanticError::import_failure(m_reporter, p.importer, item_line(p), msg);
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

    std::string unit_label(FileId f) const {
        std::string p = m_reporter.display_path(f);
        return p.empty() ? "<unknown unit>" : p;
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

    static std::string join_parts(const std::vector<std::string_view>& parts) {
        std::string out;

        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) out += "::";
            out.append(parts[i].data(), parts[i].size());
        }

        return out;
    }

    static std::string lowered(std::string_view s) {
        std::string out(s);
        for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    }

    static std::size_t edit_distance(std::string_view a, std::string_view b, std::size_t max) {
        if (a.size() > b.size()) std::swap(a, b);
        if (b.size() - a.size() > max) return max + 1;
        std::vector<std::size_t> prev(a.size() + 1), cur(a.size() + 1);
        for (std::size_t i = 0; i <= a.size(); ++i) prev[i] = i;

        for (std::size_t j = 1; j <= b.size(); ++j) {
            cur[0] = j;
            std::size_t best = cur[0];

            for (std::size_t i = 1; i <= a.size(); ++i) {
                const std::size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
                cur[i] = std::min({ cur[i - 1] + 1, prev[i] + 1, prev[i - 1] + cost });
                best = std::min(best, cur[i]);
            }

            if (best > max) return max + 1;
            prev.swap(cur);
        }

        return prev[a.size()];
    }

    static std::string nearest_export(const UnitFrontResult& tgt, const std::vector<std::string_view>& want) {
        if (want.empty()) return {};
        const std::string want_full  = join_parts(want);
        const std::string want_lower = lowered(want_full);
        const std::string leaf_lower = lowered(want.back());
        const std::size_t tol = want.back().size() <= 4 ? 1 : 2;
        std::string best;
        std::size_t best_score = static_cast<std::size_t>(-1);

        for (const semantics::ExportEntry& e : tgt.exports) {
            const std::vector<std::string_view> parts = export_public_parts(e);
            if (parts.empty()) continue;
            const std::string have_full  = join_parts(parts);
            const std::string have_lower = lowered(have_full);
            const std::string have_leaf  = lowered(parts.back());
            std::size_t score;

            if (have_lower == want_lower)      score = 0;   // case-only difference
            else if (have_leaf == leaf_lower)  score = 1;   // right leaf, wrong namespace
            else {
                const std::size_t d = edit_distance(have_leaf, leaf_lower, tol);
                if (d > tol) continue;
                score = 2 + d;
            }

            if (score < best_score) { best_score = score; best = have_full; }
            if (best_score == 0) break;
        }

        return best;
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

        if (target == INVALID_FILE) {
            semantics::SemanticError::unresolved_import_source(m_reporter, p.importer, item_line(p), it.source);
            return true;
        }

        const UnitFrontResult* tgt = unit(target);

        if (!tgt || !tgt->root) {
            semantics::SemanticError::import_target_failed(m_reporter, p.importer, item_line(p), it.source, unit_label(target));
            return true;
        }

        semantics::Symbol* ph = semantics::resolve_qualified(imp->root, local_parts(it));
        if (!ph || !ph->is_imported) return true;
        if (it.kind == IK::File) return bind_file(p, ph, *tgt);
        return bind_named(p, it, ph, *tgt);
    }

    bool bind_named(const Pending& p, const nodes::ImportExportItem& it, semantics::Symbol* ph, const UnitFrontResult& tgt) {
        if (ph->import_target) return true;
        semantics::Symbol* resolved = nullptr;
        const std::string label = unit_label(tgt.file);
        const std::string_view name = it.target_parts.empty() ? std::string_view{} : it.target_parts.back();

        if (it.kind == IK::Module) {
            resolved = semantics::resolve_qualified(tgt.root, it.target_parts);

            while (resolved && resolved->is_imported) {
                if (!resolved->import_target) return false;
                resolved = resolved->import_target;
            }

            if (!resolved) {
                semantics::SemanticError::missing_module(m_reporter, p.importer, item_line(p), name, it.source, label);
                return true;
            }
        } else {
            const semantics::ExportEntry* e = find_export(tgt, it.target_parts, it.kind);

            if (!e) {
                const std::string suggestion = nearest_export(tgt, it.target_parts);
                
                semantics::SemanticError::unexported_name(
                    m_reporter, p.importer, item_line(p),
                    name, it.source, label, tgt.exports.size(), suggestion
                );
                
                return true;
            }

            bool defer = false;
            resolved = resolve_export(tgt, *e, defer);
            if (defer) return false;

            if (!resolved) {
                semantics::SemanticError::export_undefined(m_reporter, p.importer, item_line(p), name, label);
                return true;
            }
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

        const std::string label = unit_label(tgt.file);
        semantics::Scope* ns = make_in<semantics::Scope>(m_arena, semantics::Scope::Kind::Namespace, ph->owner);

        for (const semantics::ExportEntry& e : tgt.exports) {
            if (!e.module_owner.empty()) continue;
            bool defer = false;
            semantics::Symbol* r = resolve_export(tgt, e, defer);
            std::vector<std::string_view> name = export_public_parts(e);

            if (!r) {
                semantics::SemanticError::export_undefined(
                    m_reporter, p.importer, item_line(p),
                    name.empty() ? std::string_view{} : name.back(), label
                );
                continue;
            }

            if (name.empty()) continue;
            semantics::Symbol* alias = make_in<semantics::Symbol>(m_arena, name.back(), r->kind, r->decl);
            alias->is_imported   = true;
            alias->import_target = r;
            alias->type          = r->type;
            alias->inner_scope   = r->inner_scope;

            if (semantics::Symbol* clash = ns->declare(alias)) {
                semantics::SemanticError::duplicate_export(
                    m_reporter, p.importer, item_line(p), name.back(), label,
                    clash->decl ? clash->decl->line : 0
                );
            }
        }

        ph->inner_scope = ns;
        return true;
    }

    static std::size_t item_line(const Pending& p) { return p.item->line ? p.item->line : p.decl->line; }
};

} // namespace driver
} // namespace walnut

#endif // WALNUT_DRIVER_LINKER_HPP