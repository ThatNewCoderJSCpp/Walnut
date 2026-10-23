#include "Semantics/overload.hpp"
#include <algorithm>

namespace walnut {
namespace semantics {

ParamShape shape_of(const nodes::FunctionParameter* p, TypeContext& ctx) {
    ParamShape s{};
    s.element     = ctx.canonicalize(p->get_type());
    s.is_pack     = p->is_variadic();
    s.unbounded   = s.is_pack && p->is_infinite_variadic();
    s.cap         = s.is_pack && !s.unbounded ? p->variadic_length() : 0;
    s.has_default = p->has_initializer();
    s.exact       = s.is_pack && p->is_exact_variadic() && !p->has_initializer();
    return s;
}

std::vector<ParamShape> shapes_of_function_type(const FunctionType* ft) {
    std::vector<ParamShape> out;
    if (!ft) return out;
    const std::vector<FnParamInfo>& info = ft->param_info();

    for (std::size_t i = 0; i < ft->params().size(); ++i) {
        ParamShape s{};
        Type* p = ft->params()[i];
        const FnParamInfo* pi = i < info.size() ? &info[i] : nullptr;
        s.is_pack = pi && pi->is_pack();
        s.element = s.is_pack && p && p->is_array() ? static_cast<ArrayType*>(p)->element() : p;
        s.unbounded = s.is_pack && pi->pack == FnParamInfo::Pack::Unbounded;
        s.cap = s.is_pack && !s.unbounded ? pi->cap : 0;
        s.has_default = pi && pi->def;
        s.exact = s.is_pack && pi->pack == FnParamInfo::Pack::Exact && !pi->def;
        out.push_back(s);
    }

    return out;
}

bool lex_better(const std::vector<ConversionRank>& A, const std::vector<ConversionRank>& B) {
    for (std::size_t k = 0; k < A.size(); ++k) {        
        if (A[k] < B[k]) return true;
        if (A[k] > B[k]) return false;
    }
    
    return false;
}

CandidateMatch match_arguments(const std::vector<ParamShape>& params, const std::vector<Type*>& args, TypeContext& ctx) {
    const std::size_t n = args.size(), pm = params.size();
    struct Cell { bool ok = false; std::vector<ConversionRank> ranks; };
    std::vector<Cell> prev(n + 1), cur(n + 1);
    prev[0] = Cell{ true, {} };

    auto relax = [](Cell& dst, std::vector<ConversionRank> cand) {
        if (!dst.ok || lex_better(cand, dst.ranks)) { dst.ok = true; dst.ranks = std::move(cand); }
    };

    for (std::size_t i = 0; i < pm; ++i) {
        const ParamShape& P = params[i];
        for (Cell& c : cur) c = Cell{};

        for (std::size_t a = 0; a <= n; ++a) {
            if (!prev[a].ok) continue;

            if (!P.is_pack) {
                if (a < n && !(ctx.spread_arg_element && ctx.spread_arg_element(a, n))) {
                    ConversionRank r = (!args[a] && ctx.brace_arg_rank) ? ctx.brace_arg_rank(a, P.element) : rank_conversion(args[a], P.element, ctx);

                    if (r != ConversionRank::None) {
                        std::vector<ConversionRank> v = prev[a].ranks; 
                        v.push_back(r);
                        relax(cur[a + 1], std::move(v));
                    }
                }

                if (P.has_default) relax(cur[a], prev[a].ranks);   
                continue;
            }

            if (!P.exact || P.cap == 0) relax(cur[a], prev[a].ranks);
            std::vector<ConversionRank> acc = prev[a].ranks;
            std::size_t max_take = P.unbounded ? (n - a) : std::min<std::size_t>(P.cap, n - a);

            for (std::size_t t = 1; t <= max_take; ++t) {
                Type* spread = ctx.spread_arg_element ? ctx.spread_arg_element(a + t - 1, n) : nullptr;
                if (spread && !P.unbounded) break;
                ConversionRank r = rank_conversion(spread ? spread : args[a + t - 1], P.element, ctx);
                if (r == ConversionRank::None) break;
                acc.push_back(r);
                if (!P.exact || t == P.cap) relax(cur[a + t], acc);
            }
        }

        prev.swap(cur);
    }

    CandidateMatch m;
    if (prev[n].ok) { m.viable = true; m.ranks = std::move(prev[n].ranks); }
    else            { m.ranks.assign(n, ConversionRank::None); }
    return m;
}

bool assign_arguments(const std::vector<ParamShape>& params, const std::vector<Type*>& args, TypeContext& ctx, std::vector<std::size_t>& takes) {
    const std::size_t n = args.size(), pm = params.size();
    struct Cell { bool ok = false; std::vector<ConversionRank> ranks; std::vector<std::size_t> takes; };
    std::vector<Cell> prev(n + 1), cur(n + 1);
    prev[0].ok = true;

    auto relax = [](Cell& dst, const Cell& src, std::vector<ConversionRank> cand, std::size_t took) {
        if (dst.ok && !lex_better(cand, dst.ranks)) {
            if (lex_better(dst.ranks, cand)) return;
            std::vector<std::size_t> takes = src.takes;
            takes.push_back(took);
            if (!std::lexicographical_compare(dst.takes.begin(), dst.takes.end(), takes.begin(), takes.end())) return;
        }
        dst.ok = true;
        dst.ranks = std::move(cand);
        dst.takes = src.takes;
        dst.takes.push_back(took);
    };

    for (std::size_t i = 0; i < pm; ++i) {
        const ParamShape& P = params[i];
        for (Cell& c : cur) c = Cell{};

        for (std::size_t a = 0; a <= n; ++a) {
            if (!prev[a].ok) continue;

            if (!P.is_pack) {
                if (a < n && !(ctx.spread_arg_element && ctx.spread_arg_element(a, n))) {
                    ConversionRank r = args[a] ? rank_conversion(args[a], P.element, ctx) : ConversionRank::Conversion;
                    if (r != ConversionRank::None) {
                        std::vector<ConversionRank> v = prev[a].ranks;
                        v.push_back(r);
                        relax(cur[a + 1], prev[a], std::move(v), 1);
                    }
                }
                if (P.has_default) relax(cur[a], prev[a], prev[a].ranks, 0);
                continue;
            }

            if (!P.exact || P.cap == 0) relax(cur[a], prev[a], prev[a].ranks, 0);
            std::vector<ConversionRank> acc = prev[a].ranks;
            const std::size_t max_take = P.unbounded ? (n - a) : std::min<std::size_t>(P.cap, n - a);

            for (std::size_t t = 1; t <= max_take; ++t) {
                Type* spread = ctx.spread_arg_element ? ctx.spread_arg_element(a + t - 1, n) : nullptr;
                if (spread && !P.unbounded) break;
                ConversionRank r = spread ? rank_conversion(spread, P.element, ctx) : args[a + t - 1] ? rank_conversion(args[a + t - 1], P.element, ctx) : ConversionRank::Conversion;
                if (r == ConversionRank::None) break;
                acc.push_back(r);
                if (!P.exact || t == P.cap) relax(cur[a + t], prev[a], acc, t);
            }
        }

        prev.swap(cur);
    }

    if (!prev[n].ok) return false;
    takes = std::move(prev[n].takes);
    return true;
}

bool better_candidate(const CandidateMatch& A, const CandidateMatch& B) {
    bool strictly = false;
    const std::size_t n = A.ranks.size();

    for (std::size_t k = 0; k < n; ++k) {
        if (A.ranks[k] > B.ranks[k]) return false;
        if (A.ranks[k] < B.ranks[k]) strictly = true;
    }

    return strictly;
}

std::vector<std::size_t> candidate_order(const std::vector<CandidateMatch>& cands, std::size_t chosen) {
    std::vector<std::size_t> viable;
    std::vector<std::size_t> rest;

    for (std::size_t i = 0; i < cands.size(); ++i) {
        if (i == chosen) continue;
        (cands[i].viable ? viable : rest).push_back(i);
    }

    std::vector<std::size_t> out;
    if (chosen < cands.size()) out.push_back(chosen);

    while (!viable.empty()) {
        std::size_t pick = 0;

        for (std::size_t k = 0; k < viable.size(); ++k) {
            bool beaten = false;

            for (std::size_t j = 0; j < viable.size() && !beaten; ++j) {
                if (j != k && better_candidate(cands[viable[j]], cands[viable[k]]) && !better_candidate(cands[viable[k]], cands[viable[j]])) beaten = true;
            }

            if (!beaten) { pick = k; break; }
        }

        out.push_back(viable[pick]);
        viable.erase(viable.begin() + static_cast<std::ptrdiff_t>(pick));
    }

    out.insert(out.end(), rest.begin(), rest.end());
    return out;
}

Selection select_overload(const std::vector<CandidateMatch>& cands) {
    long best = -1;

    for (std::size_t i = 0; i < cands.size(); ++i) {
        if (!cands[i].viable) continue;
        if (best < 0 || better_candidate(cands[i], cands[best])) best = long(i);
    }

    if (best < 0) return { SelectStatus::NoMatch, 0 };

    for (std::size_t i = 0; i < cands.size(); ++i) {
        if (!cands[i].viable || long(i) == best) continue;
        if (!better_candidate(cands[size_t(best)], cands[i])) return { SelectStatus::Ambiguous, 0 };
    }

    return { SelectStatus::Ok, std::size_t(best) };
}

Selection select_overload(const std::vector<CandidateMatch>& cands, const std::vector<bool>& from_template) {
    Selection sel = select_overload(cands);
    if (sel.status != SelectStatus::Ambiguous) return sel;
    std::vector<CandidateMatch> kept = cands;
    bool dropped = false;

    for (std::size_t t = 0; t < kept.size(); ++t) {
        if (!kept[t].viable || t >= from_template.size() || !from_template[t]) continue;

        for (std::size_t n = 0; n < cands.size(); ++n) {
            if (!cands[n].viable || n >= from_template.size() || from_template[n]) continue;
            if (better_candidate(cands[t], cands[n])) continue;
            kept[t].viable = false;
            dropped = true;
            break;
        }
    }

    return dropped ? select_overload(kept) : sel;
}

Selection select_overload(const std::vector<CandidateMatch>& cands, const std::vector<bool>& from_template, const std::vector<std::size_t>& fixed_params) {
    Selection sel = select_overload(cands, from_template);
    if (sel.status != SelectStatus::Ambiguous || fixed_params.size() != cands.size()) return sel;
    std::vector<CandidateMatch> kept = cands;
    bool dropped = false;

    for (std::size_t t = 0; t < kept.size(); ++t) {
        if (!kept[t].viable) continue;

        for (std::size_t n = 0; n < cands.size(); ++n) {
            if (n == t || !cands[n].viable || fixed_params[n] <= fixed_params[t]) continue;
            if (better_candidate(cands[t], cands[n])) continue;
            kept[t].viable = false;
            dropped = true;
            break;
        }
    }

    return dropped ? select_overload(kept) : sel;
}

bool variadic_signature_ok(const std::vector<ParamShape>& params, std::size_t& bad_index) {
    for (std::size_t i = 0; i + 1 < params.size(); ++i) {
        const ParamShape& P = params[i];
        const ParamShape& Q = params[i + 1];

        if (P.is_pack && P.unbounded && !Q.is_pack && !Q.has_default && Q.element == P.element) {
            bad_index = i + 1;
            return false; 
        }
    }

    return true;
}

bool signature_defaults_ok(const std::vector<ParamShape>& params, std::size_t& bad_i, std::size_t& bad_j) {
    const std::size_t pm = params.size();

    for (std::size_t i = 0; i < pm; ++i) {
        if (!params[i].has_default) continue;

        for (std::size_t j = i + 1; j < pm; ++j) {
            if (params[j].element == params[i].element && !(params[j].has_default && !params[j].is_pack)) { bad_i = i; bad_j = j; return false; }
            const bool optional = params[j].has_default || (params[j].is_pack && !params[j].exact);
            if (!optional) break;
        }
    }

    return true;
}

} // namespace semantics
} // namespace walnut
