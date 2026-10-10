#include "Parser/Types/type_qualifiers.hpp"

namespace walnut {
namespace parser_types {

void IndirectionQualifier::write_to(std::ostream& os) const {
    if (is_const) os << " const ";
    switch (kind) {
        case Kind::Pointer:         os << '*';  break;
        case Kind::Reference:       os << '&';  break;
        case Kind::RValueReference: os << "&&"; break;
    }
}

std::size_t IndirectionList::pointer_depth() const {
    std::size_t depth = 0;
    for (const auto& q : m_qualifiers) { if (q.is_pointer()) ++depth; }
    return depth;
}

} // namespace parser_types
} // namespace walnut
