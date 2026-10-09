#ifndef WALNUT_SEMA_OVERLOAD_HPP
#define WALNUT_SEMA_OVERLOAD_HPP

#include <vector>
#include "conversion.hpp"
#include "../Parser/nodes.hpp"

namespace walnut {
namespace semantics {

struct ParamShape {
    Type*       element;     
    bool        is_pack;
    bool        unbounded;   
    std::size_t cap;         
    bool        has_default;
};

inline ParamShape shape_of(const nodes::FunctionParameter* p, TypeContext& ctx) {
    ParamShape s{};
    s.element     = ctx.canonicalize(p->get_type());
    s.is_pack     = p->is_variadic();
    s.unbounded   = s.is_pack && p->is_infinite_variadic();
    s.cap         = s.is_pack && !s.unbounded ? p->variadic_length() : 0;
    s.has_default = p->has_initializer();
    return s;
}

struct CandidateMatch {
    bool                        viable = false;
    std::vector<ConversionRank> ranks;   
};

inline bool lex_better(const std::vector<ConversionRank>& A, const std::vector<ConversionRank>& B) {
    for (std::size_t k = 0; k < A.size(); ++k) {        
        if (A[k] < B[k]) return true;
        if (A[k] > B[k]) return false;
    }
    
    return false;
}

inline CandidateMatch match_arguments(const std::vector<ParamShape>& params, const std::vector<Type*>& args, TypeContext& ctx) {
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
                if (a < n) {
                    ConversionRank r = rank_conversion(args[a], P.element, ctx);

                    if (r != ConversionRank::None) {
                        std::vector<ConversionRank> v = prev[a].ranks; 
                        v.push_back(r);
                        relax(cur[a + 1], std::move(v));
                    }
                }

                if (P.has_default) relax(cur[a], prev[a].ranks);   
                continue;
            }

            relax(cur[a], prev[a].ranks);                         
            std::vector<ConversionRank> acc = prev[a].ranks;
            std::size_t max_take = P.unbounded ? (n - a) : std::min<std::size_t>(P.cap, n - a);

            for (std::size_t t = 1; t <= max_take; ++t) {
                ConversionRank r = rank_conversion(args[a + t - 1], P.element, ctx);
                if (r == ConversionRank::None) break;
                acc.push_back(r);
                relax(cur[a + t], acc);
            }
        }

        prev.swap(cur);
    }

    CandidateMatch m;
    if (prev[n].ok) { m.viable = true; m.ranks = std::move(prev[n].ranks); }
    else            { m.ranks.assign(n, ConversionRank::None); }
    return m;
}

inline bool assign_arguments(const std::vector<ParamShape>& params, const std::vector<Type*>& args, TypeContext& ctx, std::vector<std::size_t>& takes) {
    const std::size_t n = args.size(), pm = params.size();
    struct Cell { bool ok = false; std::vector<ConversionRank> ranks; std::vector<std::size_t> takes; };
    std::vector<Cell> prev(n + 1), cur(n + 1);
    prev[0].ok = true;

    auto relax = [](Cell& dst, const Cell& src, std::vector<ConversionRank> cand, std::size_t took) {
        if (dst.ok && !lex_better(cand, dst.ranks)) return;
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
                if (a < n) {
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

            relax(cur[a], prev[a], prev[a].ranks, 0);
            std::vector<ConversionRank> acc = prev[a].ranks;
            const std::size_t max_take = P.unbounded ? (n - a) : std::min<std::size_t>(P.cap, n - a);

            for (std::size_t t = 1; t <= max_take; ++t) {
                ConversionRank r = args[a + t - 1] ? rank_conversion(args[a + t - 1], P.element, ctx) : ConversionRank::Conversion;
                if (r == ConversionRank::None) break;
                acc.push_back(r);
                relax(cur[a + t], prev[a], acc, t);
            }
        }

        prev.swap(cur);
    }

    if (!prev[n].ok) return false;
    takes = std::move(prev[n].takes);
    return true;
}

inline bool better_candidate(const CandidateMatch& A, const CandidateMatch& B) {
    bool strictly = false;
    const std::size_t n = A.ranks.size();

    for (std::size_t k = 0; k < n; ++k) {
        if (A.ranks[k] > B.ranks[k]) return false;
        if (A.ranks[k] < B.ranks[k]) strictly = true;
    }

    return strictly;
}

enum class SelectStatus : std::uint8_t { Ok = 0, NoMatch, Ambiguous };
struct Selection { SelectStatus status; std::size_t index = 0; };

inline Selection select_overload(const std::vector<CandidateMatch>& cands) {
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

inline bool variadic_signature_ok(const std::vector<ParamShape>& params, std::size_t& bad_index) {
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

inline bool signature_defaults_ok(const std::vector<ParamShape>& params, std::size_t& bad_i, std::size_t& bad_j) {
    const std::size_t pm = params.size();

    for (std::size_t i = 0; i < pm; ++i) {
        if (params[i].is_pack || !params[i].has_default) continue;
        
        for (std::size_t j = i + 1; j < pm; ++j) {
            if (params[j].is_pack) break;
            if (params[j].element == params[i].element) { bad_i = i; bad_j = j; return false; }
            if (!params[j].has_default) break;   
        }
    }

    return true;
}

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMA_OVERLOAD_HPP