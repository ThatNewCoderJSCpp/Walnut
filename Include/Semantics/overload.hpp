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

ParamShape shape_of(const nodes::FunctionParameter* p, TypeContext& ctx);

struct CandidateMatch {
    bool                        viable = false;
    std::vector<ConversionRank> ranks;   
};

bool lex_better(const std::vector<ConversionRank>& A, const std::vector<ConversionRank>& B);

CandidateMatch match_arguments(const std::vector<ParamShape>& params, const std::vector<Type*>& args, TypeContext& ctx);

bool assign_arguments(const std::vector<ParamShape>& params, const std::vector<Type*>& args, TypeContext& ctx, std::vector<std::size_t>& takes);

bool better_candidate(const CandidateMatch& A, const CandidateMatch& B);

enum class SelectStatus : std::uint8_t { Ok = 0, NoMatch, Ambiguous };
struct Selection { SelectStatus status; std::size_t index = 0; };

Selection select_overload(const std::vector<CandidateMatch>& cands);

bool variadic_signature_ok(const std::vector<ParamShape>& params, std::size_t& bad_index);

bool signature_defaults_ok(const std::vector<ParamShape>& params, std::size_t& bad_i, std::size_t& bad_j);

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMA_OVERLOAD_HPP